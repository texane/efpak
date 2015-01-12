#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include "zlib.h"
#include "decomp.h"


#ifdef DECOMP_UNIT
#include <stdio.h>
#define PERROR()				\
do {						\
  fprintf(stderr, "[!] %u\n", __LINE__);	\
  fflush(stderr);				\
} while (0)
#elif 1
#include "log.h"
#define PERROR() LOG_ERROR("")
#else
#define PERROR()
#endif


static void reset_partial(decomp_handle_t* decomp)
{
  z_stream* const z = &decomp->z;

  decomp->flags = 0;

  z->next_in = Z_NULL;
  z->avail_in = 0;

  z->next_out = (Bytef*)decomp->obuf;
  z->avail_out = (uInt)decomp->osize;
}

int decomp_init
(decomp_handle_t* decomp, size_t osize)
{
  /* initialize a decompressor */

  z_stream* const z = &decomp->z;

  if (osize == 0) osize = 8 * 1024;
  decomp->osize = osize;
  decomp->obuf = malloc(osize);
  if (decomp->obuf == NULL)
  {
    PERROR();
    goto on_error_0;
  }

  reset_partial(decomp);

  z->zalloc = Z_NULL;
  z->zfree = Z_NULL;
  z->opaque = Z_NULL;

  /* http://stackoverflow.com/questions/1838699/ */
  /* how-can-i-decompress-a-gzip-stream-with-zlib */
  if (inflateInit2(z, 16 + MAX_WBITS) != Z_OK)
  {
    PERROR();
    goto on_error_1;
  }

  return 0;

 on_error_1:
  free(z->next_out);
 on_error_0:
  return -1;
}

int decomp_fini
(decomp_handle_t* decomp)
{
  z_stream* const z = &decomp->z;

  if (inflateEnd(z) != Z_OK)
  {
    PERROR();
    return -1;
  }

  free(decomp->obuf);

  return 0;
}

int decomp_reset
(decomp_handle_t* decomp)
{
  z_stream* const z = &decomp->z;

  if (inflateReset(z) != Z_OK)
  {
    PERROR();
    return -1;
  }

  reset_partial(decomp);

  return 0;
}

int decomp_add_iblock
(decomp_handle_t* decomp, uint8_t* ibuf, size_t isize)
{
  /* note: the current input block should be fully consumed */
  /* note: ibuf is still referenced until fully consumed */

  z_stream* const z = &decomp->z;

  z->next_in = ibuf;
  z->avail_in = isize;

  return 0;
}

void decomp_set_eoi
(decomp_handle_t* decomp)
{
  /* end of input */

  decomp->flags |= DECOMP_FLAG_EOI;
}

unsigned int decomp_is_done
(decomp_handle_t* decomp)
{
  z_stream* const z = &decomp->z;

  if (z->avail_out == decomp->osize)
    if (z->avail_in == 0)
      if (decomp->flags & DECOMP_FLAG_EOI)
	return 1;

  return 0;
}

int decomp_set_single_iblock
(decomp_handle_t* decomp, uint8_t* ibuf, size_t isize)
{
  if (decomp_add_iblock(decomp, ibuf, isize)) return -1;
  decomp_set_eoi(decomp);
  return 0;
}

int decomp_next_oblock
(decomp_handle_t* decomp, const uint8_t** obufp, size_t* osizep)
{
  /* get the next output block */

  /* the output buffer is produced only if the size equals */
  /* decomp->osize or there is on more input left */

  z_stream* const z = &decomp->z;

  /* produce output buffer from input */

  while (z->avail_in)
  {
    const int err = inflate(z, 0);
    if ((err != Z_STREAM_END) && (err != Z_OK))
    {
      PERROR();
      return -1;
    }

    if (z->avail_out == 0) break ;
  }

  if ((z->avail_out == 0) || (decomp->flags & DECOMP_FLAG_EOI))
  {
    *obufp = decomp->obuf;
    *osizep = decomp->osize - (size_t)z->avail_out;
    z->next_out = decomp->obuf;
    z->avail_out = decomp->osize;
  }
  else
  {
    *obufp = NULL;
    *osizep = 0;
  }

  return 0;
}

#ifdef DECOMP_UNIT

#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>

int main
(int ac, char** av)
{
  decomp_handle_t decomp;
  int err = -1;
  const uint8_t* obuf;
  size_t size;
  size_t isize;
  uint8_t* ibuf;
  int ifd = 0;
  int ofd = 1;

  if (decomp_init(&decomp, 16 * 1024))
  {
    PERROR();
    goto on_error_0;
  }

  if (decomp_reset(&decomp))
  {
    PERROR();
    goto on_error_0;
  }

#if 0 /* iterative scheme, when the input is received by block */

  isize = 4 * 1024;
  ibuf = malloc(isize);
  
  while (decomp_is_done(&decomp) == 0)
  {
#if 1 /* random sizes */
    size = 1 + ((size_t)rand()) % (isize - 1);
#else
    size = isize;
#endif
    size = (size_t)read(ifd, ibuf, size);

    if (size < 0)
    {
      PERROR();
      goto on_error_1;
    }
    else if (size == 0)
    {
      decomp_set_eoi(&decomp);
    }
    else if (decomp_add_iblock(&decomp, ibuf, size))
    {
      PERROR();
      goto on_error_1;
    }

    while (1)
    {
      if (decomp_next_oblock(&decomp, &obuf, &size))
      {
	PERROR();
	goto on_error_1;
      }
      if (size == 0) break ;
      write(ofd, obuf, size);
    }
  }

  free(ibuf);

#else /* direct scheme, when there is only one input block */

  struct stat st;

  ifd = open(av[1], O_RDONLY);
  if (ifd == -1)
  {
    PERROR();
    goto on_error_1;
  }

  if (fstat(ifd, &st))
  {
    PERROR();
    close(ifd);
    goto on_error_1;
  }

  isize = st.st_size;
  ibuf = malloc(isize);

  size = (size_t)read(ifd, ibuf, isize);

  close(ifd);

  if (size != isize)
  {
    PERROR();
    goto on_error_1;
  }

  if (decomp_set_single_iblock(&decomp, ibuf, isize))
  {
    PERROR();
    goto on_error_1;
  }

  while (1)
  {
    if (decomp_next_oblock(&decomp, &obuf, &size))
    {
      PERROR();
      goto on_error_1;
    }
    if (size == 0) break ;
    write(ofd, obuf, size);
  }

#endif

  err = 0;
 on_error_1:
  decomp_fini(&decomp);
 on_error_0:
  return err;
}

#endif /* DECOMP_UNIT */
