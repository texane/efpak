#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>


#include 


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



/* tool to manage efpak files */


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
