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


/* file mapping */

static int map_file(const char* path, const uint8_t** addr, size_t* size)
{
  struct stat st;
  int fd;
  int err = -1;

  fd = open(path, O_RDONLY);
  if (fd == -1) goto on_error_0;

  if (fstat(fd, &st)) goto on_error_1;

  *size = st.st_size;
  *addr = mmap(NULL, *size, PROT_READ, MAP_SHARED, fd, 0);
  if (*addr == (const uint8_t*)MAP_FAILED) goto on_error_1;

  err = 0;

 on_error_1:
  close(fd);
 on_error_0:
  return err;
}

static void unmap_file(const uint8_t* addr, size_t size)
{
  munmap((void*)addr, size);
}


/* input stream exported routines */

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
  const uint8_t* addr;
  size_t size;
  int err;

  if (map_file(s, &addr, &size))
  {
    PERROR();
    return -1;
  }

  err = efpak_istream_init_with_mem(is, addr, size);
  unmap_file(addr, size);

  return err;
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
    const size_t off =
      is->off + is->header->header_size + is->header->comp_data_size;
    if (off > is->size) return -1;
    is->off += off;
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

  if ((is->off + h->header_size + h->comp_data_size) > is->size)
    goto on_error;

  data = is->data + is->off + h->header_size;
  size = h->comp_data_size;

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


/* output stream exported routines */

static int deflateInit2Default(z_stream* z)
{
  return deflateInit2
    (z, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
     16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
}

static int deflate_mem
(const uint8_t* idata, size_t isize, const uint8_t** odata, size_t* osize)
{
  uLong dlen;
  z_stream z;
  int err = -1;

  dlen = compressBound((uLong)isize);

  *odata = malloc((size_t)dlen);
  if (*odata == NULL) goto on_error_0;

  z.next_in = (Bytef*)idata;
  z.avail_in = (uInt)isize;

  z.next_out = (Bytef*)*odata;
  z.avail_out = (uInt)*osize;

  z.zalloc = Z_NULL;
  z.zfree = Z_NULL;
  z.opaque = Z_NULL;

  if (deflateInit2Default(&z) != Z_OK) goto on_error_1;
  if (deflate(&z, 0) != Z_OK) goto on_error_2;

  *osize = (size_t)dlen;
  err = 0;

 on_error_2:
  deflateEnd(&z);
 on_error_1:
  if (err) free((void*)*odata);
 on_error_0:
  return err;
}

static int deflate_file_if_large
(
 const char* path,
 const uint8_t** odata, size_t* isize, size_t* osize,
 unsigned int* is_deflated
)
{
  const uint8_t* idata;

  if (map_file(path, &idata, isize)) return -1;

  /* compress file larger than 64KB */
  if (*isize > (64 * 1024))
  {
    const int err = deflate_mem(idata, *isize, odata, osize);
    unmap_file(idata, *isize);
    if (err) return -1;
    *is_deflated = 1;
  }
  else
  {
    *odata = idata;
    *osize = *isize;
    *is_deflated = 0;
  }

  return 0;
}

static void init_header
(efpak_header_t* h)
{
  h->vers = 0;
}

static int add_block
(efpak_ostream_t* os, const efpak_header_t* header, const uint8_t* data)
{
  ssize_t res;

  /* TODO: convert header fields if local endianness is not little */
#if (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error "unsupported endianness"
#endif

  res = write(os->fd, (const uint8_t*)header, header->header_size);
  if (res != (ssize_t)header->header_size) return -1;

  if (data != NULL)
  {
    res = write(os->fd, data, header->comp_data_size);
    if (res != (ssize_t)header->comp_data_size) return -1;
  }

  return 0;
}

static const size_t header_min_size = offsetof(efpak_header_t, u.per_type);

static int efpak_ostream_add_format
(efpak_ostream_t* os)
{
  efpak_header_t h;

  init_header(&h);

  h.type = EFPAK_BTYPE_FORMAT;
  h.comp = EFPAK_BCOMP_NONE;
  h.header_size = header_min_size + sizeof(efpak_format_header_t);
  h.comp_data_size = 0;
  h.raw_data_size = 0;

  memcpy(h.u.format.signature, EFPAK_FORMAT_SIGNATURE, 4);
  h.u.format.vers = 0;

  return add_block(os, &h, NULL);
}

int efpak_ostream_init_with_file
(efpak_ostream_t* os, const char* path)
{
  off_t off;

  os->fd = open(path, O_RDWR | O_CREAT, 0755);
  if (os->fd == -1) goto on_error_0;

  off = lseek(os->fd, 0, SEEK_END);
  if (off == (off_t)-1) goto on_error_1;

  /* add header in newly created file */
  if ((off == 0) && efpak_ostream_add_format(os)) goto on_error_1;

  return 0;

 on_error_1:
  close(os->fd);
 on_error_0:
  return -1;
}

void efpak_ostream_fini
(efpak_ostream_t* os)
{
  close(os->fd);
}

int efpak_ostream_add_disk
(efpak_ostream_t* os, const char* path)
{
  efpak_header_t h;
  unsigned int is_comp;
  const uint8_t* data;
  size_t comp_size;
  size_t raw_size;
  int err = -1;

  if (deflate_file_if_large(path, &data, &raw_size, &comp_size, &is_comp))
    goto on_error_0;

  init_header(&h);

  h.type = EFPAK_BTYPE_DISK;
  h.comp = is_comp ? EFPAK_BCOMP_ZLIB : EFPAK_BCOMP_NONE;
  h.header_size = header_min_size + sizeof(efpak_disk_header_t);
  h.comp_data_size = comp_size;
  h.raw_data_size = raw_size;

  if (add_block(os, &h, data)) goto on_error_1;

  err = 0;

 on_error_1:
  if (is_comp) free((void*)data);
  else unmap_file(data, comp_size);
 on_error_0:
  return err;
}

int efpak_ostream_add_part
(efpak_ostream_t* os, const char* path, efpak_partid_t id)
{
  efpak_header_t h;
  unsigned int is_comp;
  const uint8_t* data;
  size_t comp_size;
  size_t raw_size;
  int err = -1;

  if (deflate_file_if_large(path, &data, &raw_size, &comp_size, &is_comp))
    goto on_error_0;

  init_header(&h);

  h.type = EFPAK_BTYPE_PART;
  h.comp = is_comp ? EFPAK_BCOMP_ZLIB : EFPAK_BCOMP_NONE;
  h.header_size = header_min_size + sizeof(efpak_part_header_t);
  h.comp_data_size = comp_size;
  h.raw_data_size = raw_size;

  h.u.part.id = id;

  if (add_block(os, &h, data)) goto on_error_1;

  err = 0;

 on_error_1:
  if (is_comp) free((void*)data);
  else unmap_file(data, comp_size);
 on_error_0:
  return err;
}

int efpak_ostream_add_file
(efpak_ostream_t* os, const char* lpath, const char* dpath)
{
  /* lpath the local path */
  /* dpath the destination path */

  efpak_header_t* h;
  size_t header_size;
  unsigned int is_comp;
  const uint8_t* data;
  size_t comp_size;
  size_t raw_size;
  size_t len;
  int err = -1;

  len = strlen(dpath) + 1;
  header_size = header_min_size + offsetof(efpak_file_header_t, path) + len;
  h = malloc(header_size);
  if (h == NULL) goto on_error_0;

  if (deflate_file_if_large(lpath, &data, &raw_size, &comp_size, &is_comp))
    goto on_error_1;

  init_header(h);

  h->type = EFPAK_BTYPE_FILE;
  h->comp = is_comp ? EFPAK_BCOMP_ZLIB : EFPAK_BCOMP_NONE;
  h->header_size = header_size;
  h->comp_data_size = comp_size;
  h->raw_data_size = raw_size;

  h->u.file.path_len = len;
  strcpy((char*)h->u.file.path, dpath);

  if (add_block(os, h, data)) goto on_error_2;

  err = 0;

 on_error_2:
  if (is_comp) free((void*)data);
  else unmap_file(data, comp_size);
 on_error_1:
  free(h);
 on_error_0:
  return err;
}
