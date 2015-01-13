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
#include "decomp.h"


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


/* input stream */

typedef struct efpak_istream
{
  const uint8_t* data;
  size_t size;
  const efpak_header_t* h;
} efpak_handle_t;


/* per memory type specific operations */

typedef struct mem_handle
{
  /* common */
  const uint8_t* data;
  size_t size;
  size_t off;

  /* zmem specific */
  decomp_handle_t decomp;
  const uint8_t* zmem_data;
  size_t zmem_size;

  int (*seek)(struct mem_handle*, size_t);
  int (*next)(struct mem_handle*, const uint8_t**, size_t*);
  void (*fini)(struct mem_handle*);

} mem_handle_t;

static void mem_init(mem_handle_t* mem, const uint8_t* data, size_t size) 
{
  mem->data = data;
  mem->size = size;
  mem->off = 0;
}

static int ram_seek(mem_handle_t* mem, size_t off)
{
  if (off > mem->size) return -1;
  mem->off = off;
  return 0;
}

static int ram_next(mem_handle_t* mem, const uint8_t** data, size_t* size)
{
  if (*size == (size_t)-1) *size = mem->size - mem->off;
  if ((mem->off + *size) > mem->size) *size = mem->size - mem->off;

  *data = mem->data + mem->off;
  mem->off += *size;

  return 0;
}

static void ram_fini(mem_handle_t* mem)
{
}

static int ram_init(mem_handle_t* mem, const uint8_t* data, size_t size)
{
  mem_init(mem, data, size);
  mem->seek = ram_seek;
  mem->next = ram_next;
  mem->fini = ram_fini;
  return 0;
}

static int zmem_seek(mem_handle_t* mem, size_t off)
{
  size_t n;

  if ((mem->off + mem->zmem_size) <= off)
  {
    while (1)
    {
      mem->off += mem->zmem_size;

      if (decomp_next_oblock(&mem->decomp, &mem->zmem_data, &mem->zmem_size))
	return -1;

      if (mem->zmem_size == 0)
	return -1;

      if ((mem->off + mem->zmem_size) > off)
	break ;
    }
  }

  n = off - mem->off;
  mem->zmem_data += n;
  mem->zmem_size -= n;
  mem->off += n;

  return 0;
}

static int zmem_next(mem_handle_t* mem, const uint8_t** buf, size_t* size)
{
  if (mem->zmem_size == 0)
  {
    if (decomp_next_oblock(&mem->decomp, &mem->zmem_data, &mem->zmem_size))
      return -1;
  }

  /* note: keep both condition for readability */
  if ((*size == (size_t)-1) || (*size > mem->zmem_size))
  {
    *size = mem->zmem_size;
  }

  *buf = mem->zmem_data;

  mem->off += *size;
  mem->zmem_data += *size;
  mem->zmem_size -= *size;

  return 0;
}

static void zmem_fini(mem_handle_t* mem)
{
  decomp_fini(&mem->decomp);
}

static int zmem_init(mem_handle_t* mem, const uint8_t* data, size_t size)
{
  /* ASSUME((oblock_size % DISK_BLOCK_SIZE) == 0) */
  static const size_t oblock_size = 64 * 1024;

  if (decomp_init(&mem->decomp, oblock_size))
    goto on_error_0;

  if (decomp_set_single_iblock(&mem->decomp, (void*)data, size))
    goto on_error_1;

  mem_init(mem, data, size);
  mem->seek = zmem_seek;
  mem->next = zmem_next;
  mem->fini = zmem_fini;

  mem->zmem_data = NULL;
  mem->zmem_size = 0;

  return 0;

 on_error_1:
  decomp_fini(&mem->decomp);
 on_error_0:
  return -1;
}


/* exported routines */

int efpak_istream_init_with_file
(efpak_istream_t* is, const char* s)
{
  return -1;
}

int efpak_istream_fini
(efpak_istream_t* is)
{
  return -1;
}

int efpak_istream_start_block
(efpak_istream_t* is, const efpak_header_t* h)
{
  /* TODO: prepare everything related to the block, esp. mem ops */
  return -1;
}

int efpak_istream_end_block
(efpak_istream_t* is)
{
  return -1;
}

int efpak_istream_seek
(efpak_handle_t* efpak, size_t off)
{
  return efpak->mem.seek(&efpak->mem, off);
}

int efpak_istream_next
(efpak_handle_t* efpak, const uint8_t** data, size_t* size)
{
  return efpak->mem.next(&efpak->mem, data, size);
}



#ifdef EFPAK_UNIT

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

int main(int ac, char** av)
{
  efpak_handle_t efpak;

  efpak_init();

  while (efpak_)

  return 0;
}

#endif /* EFPAK_UNIT */
