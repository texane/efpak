#define _BSD_SOURCE
#define _LARGEFILE64_SOURCE

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include "efpak.h"


#ifdef EFPAK_UNIT
#include <stdio.h>
#define PERROR()			\
do {					\
  printf("[!] %u\n", __LINE__);		\
  fflush(stdout);			\
} while (0)
#elif 1
#include "log.h"
#define PERROR() LOG_ERROR("")
#else
#define PERROR()
#endif


/* zlib deflate routines */

static void inflate_reset_partial(efpak_inflate_t* inflate)
{
  z_stream* const z = &inflate->z;

  inflate->flags = 0;

  z->next_in = Z_NULL;
  z->avail_in = 0;

  z->next_out = (Bytef*)inflate->obuf;
  z->avail_out = (uInt)inflate->osize;
}

static int inflate_init
(efpak_inflate_t* inflate, size_t osize)
{
  /* initialize a inflateressor */

  z_stream* const z = &inflate->z;

  if (osize == 0) osize = 8 * 1024;
  inflate->osize = osize;
  inflate->obuf = malloc(osize);
  if (inflate->obuf == NULL)
  {
    PERROR();
    goto on_error_0;
  }

  inflate_reset_partial(inflate);

  z->zalloc = Z_NULL;
  z->zfree = Z_NULL;
  z->opaque = Z_NULL;

  /* http://stackoverflow.com/questions/1838699/ */
  /* how-can-i-inflateress-a-gzip-stream-with-zlib */
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

static int inflate_fini
(efpak_inflate_t* inflate)
{
  z_stream* const z = &inflate->z;

  if (inflateEnd(z) != Z_OK)
  {
    PERROR();
    return -1;
  }

  free(inflate->obuf);

  return 0;
}

__attribute__((unused))
static int inflate_reset
(efpak_inflate_t* inflate)
{
  z_stream* const z = &inflate->z;

  if (inflateReset(z) != Z_OK)
  {
    PERROR();
    return -1;
  }

  inflate_reset_partial(inflate);

  return 0;
}

static int inflate_add_iblock
(efpak_inflate_t* inflate, uint8_t* ibuf, size_t isize)
{
  /* note: the current input block should be fully consumed */
  /* note: ibuf is still referenced until fully consumed */

  z_stream* const z = &inflate->z;

  z->next_in = ibuf;
  z->avail_in = isize;

  return 0;
}

static void inflate_set_eoi
(efpak_inflate_t* inflate)
{
  /* end of input */

  inflate->flags |= EFPAK_INFLATE_FLAG_EOI;
}

__attribute__((unused))
static unsigned int inflate_is_done
(efpak_inflate_t* inflate)
{
  z_stream* const z = &inflate->z;

  if (z->avail_out == inflate->osize)
    if (z->avail_in == 0)
      if (inflate->flags & EFPAK_INFLATE_FLAG_EOI)
	return 1;

  return 0;
}

static int inflate_set_single_iblock
(efpak_inflate_t* inflate, uint8_t* ibuf, size_t isize)
{
  if (inflate_add_iblock(inflate, ibuf, isize)) return -1;
  inflate_set_eoi(inflate);
  return 0;
}

static int inflate_next_oblock
(efpak_inflate_t* infl, const uint8_t** obufp, size_t* osizep)
{
  /* get the next output block */

  /* the output buffer is produced only if the size equals */
  /* inflate->osize or there is on more input left */

  z_stream* const z = &infl->z;

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

  if ((z->avail_out == 0) || (infl->flags & EFPAK_INFLATE_FLAG_EOI))
  {
    *obufp = infl->obuf;
    *osizep = infl->osize - (size_t)z->avail_out;
    z->next_out = infl->obuf;
    z->avail_out = infl->osize;
  }
  else
  {
    *obufp = NULL;
    *osizep = 0;
  }

  return 0;
}


/* block memory type specific operations */

static void mem_init(efpak_imem_t* mem, const uint8_t* data, size_t size) 
{
  mem->data = data;
  mem->size = size;
  mem->off = 0;
}

static int ram_mem_seek(efpak_imem_t* mem, size_t off)
{
  if (off > mem->size) return -1;
  mem->off = off;
  return 0;
}

static int ram_mem_next(efpak_imem_t* mem, const uint8_t** data, size_t* size)
{
  if (*size == (size_t)-1) *size = mem->size - mem->off;
  if ((mem->off + *size) > mem->size) *size = mem->size - mem->off;

  *data = mem->data + mem->off;
  mem->off += *size;

  return 0;
}

static void ram_mem_fini(efpak_imem_t* mem)
{
}

static int ram_mem_init(efpak_imem_t* mem, const uint8_t* data, size_t size)
{
  mem_init(mem, data, size);
  mem->seek = ram_mem_seek;
  mem->next = ram_mem_next;
  mem->fini = ram_mem_fini;
  return 0;
}

static int inflate_mem_seek(efpak_imem_t* mem, size_t off)
{
  size_t n;

  if ((mem->off + mem->inflate_size) <= off)
  {
    while (1)
    {
      mem->off += mem->inflate_size;

      if (inflate_next_oblock
	  (&mem->inflate, &mem->inflate_data, &mem->inflate_size))
	return -1;

      if (mem->inflate_size == 0)
	return -1;

      if ((mem->off + mem->inflate_size) > off)
	break ;
    }
  }

  n = off - mem->off;
  mem->inflate_data += n;
  mem->inflate_size -= n;
  mem->off += n;

  return 0;
}

static int inflate_mem_next
(efpak_imem_t* mem, const uint8_t** buf, size_t* size)
{
  if (mem->inflate_size == 0)
  {
    if (inflate_next_oblock
	(&mem->inflate, &mem->inflate_data, &mem->inflate_size))
      return -1;
  }

  /* note: keep both condition for readability */
  if ((*size == (size_t)-1) || (*size > mem->inflate_size))
  {
    *size = mem->inflate_size;
  }

  *buf = mem->inflate_data;

  mem->off += *size;
  mem->inflate_data += *size;
  mem->inflate_size -= *size;

  return 0;
}

static void inflate_mem_fini(efpak_imem_t* mem)
{
  inflate_fini(&mem->inflate);
}

static int inflate_mem_init
(efpak_imem_t* mem, const uint8_t* data, size_t size)
{
  /* ASSUME((oblock_size % DISK_BLOCK_SIZE) == 0) */
  static const size_t oblock_size = 64 * 1024;

  if (inflate_init(&mem->inflate, oblock_size))
    goto on_error_0;

  if (inflate_set_single_iblock(&mem->inflate, (void*)data, size))
    goto on_error_1;

  mem_init(mem, data, size);
  mem->seek = inflate_mem_seek;
  mem->next = inflate_mem_next;
  mem->fini = inflate_mem_fini;

  mem->inflate_data = NULL;
  mem->inflate_size = 0;

  return 0;

 on_error_1:
  inflate_fini(&mem->inflate);
 on_error_0:
  return -1;
}


/* input stream */

/* exported routines */

int efpak_istream_init_with_mem
(efpak_istream_t* is, const uint8_t* data, size_t size)
{
  is->data = data;
  is->off = 0;
  is->size = size;
  is->header = NULL;
  is->is_in_block = 0;
  return 0;
}

int efpak_istream_init_with_file
(efpak_istream_t* is, const char* s)
{
  return -1;
}

void efpak_istream_fini
(efpak_istream_t* is)
{
  if (is->is_in_block == 1) efpak_istream_end_block(is);
}

int efpak_istream_next_block
(efpak_istream_t* is, const efpak_header_t** h)
{
  if (is->header != NULL)
  {
    if ((is->off + is->header->comp_block_size) > is->size) return -1;
    is->off += is->header->comp_block_size;
  }

  /* TODO: convert header fields if local endianness is not little */
  /* TODO: to do so, first internally allocate a header, convert */
  /* TODO: the fields and return this allocated header. take care */
  /* TODO: of releasing when no longer needed. */

#if (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error "unsupported endianness"
#endif

  is->header = (const efpak_header_t*)(is->data + is->off);
  if ((is->off + is->header->header_size) > is->size) return -1;

  *h = is->header;

  return 0;
}

int efpak_istream_start_block
(efpak_istream_t* is)
{
  /* prepare block related context */

  const efpak_header_t* const h = is->header;
  const uint8_t* data;
  size_t size;
  int err = -1;

  /* ASSUME: is->is_in_block == 0 */

  /* TODO: check sum overflow */

  if (h->header_size > h->comp_block_size) goto on_error;
  if ((is->off + h->header_size) > is->size) goto on_error;

  data = is->data + is->off + h->header_size;
  size = h->comp_block_size - h->header_size;

  switch ((efpak_bcomp_t)is->header->comp)
  {
  case EFPAK_BCOMP_NONE:
    {
      err = ram_mem_init(&is->mem, data, size);
      break ;
    }

  case EFPAK_BCOMP_ZLIB:
    {
      err = inflate_mem_init(&is->mem, data, size);
      break ;
    }

  default:
    {
      PERROR();
      goto on_error;
      break ;
    }
  }

  if (err == 0) is->is_in_block = 1;

 on_error:
  return err;
}

void efpak_istream_end_block
(efpak_istream_t* is)
{
  /* ASSUME: is->is_in_block == 1 */

  is->mem.fini(&is->mem);
}

int efpak_istream_seek
(efpak_istream_t* is, size_t off)
{
  /* ASSUME: is->is_in_block == 1 */

  return is->mem.seek(&is->mem, off);
}

int efpak_istream_next
(efpak_istream_t* is, const uint8_t** data, size_t* size)
{
  /* ASSUME: is->is_in_block == 1 */

  return is->mem.next(&is->mem, data, size);
}



#ifdef EFPAK_UNIT

#if 0
#include "disk.h"

typedef struct cmdline_info
{
  size_t block_count;

#define MAX_BLOCK_COUNT 32
  uint32_t block_types[MAX_BLOCK_COUNT];

} cmdline_info_t;

static int do_info(efpak_handle_t* efpak, const cmdline_info_t* ci)
{
  return -1;
}

static int do_create(efpak_handle_t* efpak, const cmdline_info_t* ci)
{
  return -1;
}

static int disk_update_with_efpak(const cmdline_info_t* ci)
{
  efpak_istream_t is;
  const efpak_header_t* h;
  unsigned int is_new_mbr = 0;
  mbr_t new_mbr;

  if (efpak_istream_init_with_file(&is, ci->ipath))
  {
    PERROR();
    goto on_error_0;
  }

  while (1)
  {
    if (efpak_istream_next_block(&is, &h))
    {
      PERROR();
      goto on_error_1;
    }

    /* last block */
    if (h == NULL) break ;

    switch (h->type)
    {
    case EFPAK_BTYPE_FORMAT:
      {
	/* TODO: check format information, esp. signature and version */
	break ;
      }

    case EFPAK_BTYPE_DISK:
      {
	efpak_istream_start_block(&is, h);
	disk_read(disk, 0, &new_mbr);
	update_disk(&is, h, &new_mbr);
	is_new_mbr = 1;
	efpak_istream_end_block(&is);

	break ;
      }

    case EFPAK_BTYPE_PART:
      {
	efpak_istream_start_block(&is, h);
	update_part(&new_mbr);
	efpak_istream_start_block(&is);

	if (is_new_mbr == 0) disk_read(disk, 0, &new_mbr);
	/* TODO: update new_mbr in memory contents */
	is_new_mbr = 1;

	break ;
      }

    case EFPAK_BTYPE_FILE:
      {
	break ;
      }

    default:
      {
	/* skip the block */
	break ;
      }
    }
  }

  if (is_new_mbr)
  {
    disk_write(disk, &new_mbr);
  }

 on_error_1:
  efpak_istream_fini(&is);
 on_error_0:
  return err;
}

static int do_update_disk(const cmdline_info_t* ci)
{
  /* TODO: map the file */
  return disk_update_with_efpak(&disk, data, ci);
}
#endif /* 0 */

int main(int ac, char** av)
{
  return 0;
}

#endif /* EFPAK_UNIT */
