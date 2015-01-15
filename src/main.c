/* tool to manage efpak files */


#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "efpak.h"
#include "disk.h"


#include <stdio.h>
#define PERROR()			\
do {					\
  printf("[!] %u\n", __LINE__);		\
  fflush(stdout);			\
} while (0)


#if 0
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
#endif /* 0 */

static int do_list_headers(int ac, const char** av)
{
  const char* const path = av[2];
  efpak_istream_t is;
  const efpak_header_t* h;
  int err = -1;

  if (ac != 3) goto on_error_0;

  if (efpak_istream_init_with_file(&is, path)) goto on_error_0;

  while (1)
  {
    if (efpak_istream_next_block(&is, &h)) goto on_error_1;
    if (h == NULL) break ;

    printf("header:\n");
    printf(".vers          : 0x%02x\n", h->vers);
    printf(".type          : 0x%02x\n", h->type);
    printf(".comp          : 0x%02x\n", h->comp);
    printf(".header_size   : %" PRIu64 "\n", h->header_size);
    printf(".comp_data_size: %" PRIu64 "\n", h->comp_data_size);
    printf(".raw_data_size : %" PRIu64 "\n", h->raw_data_size);

    switch (h->type)
    {
    case EFPAK_BTYPE_FORMAT:
      {
	const uint8_t* const s = h->u.format.signature;
	printf(".signature: %c%c%c%c\n", s[0], s[1], s[2], s[3]);
	break ;
      }

    case EFPAK_BTYPE_DISK:
      {
	break ;
      }

    case EFPAK_BTYPE_PART:
      {
	printf(".id            : 0x%02x\n", h->u.part.id);
	break ;
      }

    case EFPAK_BTYPE_FILE:
      {
	const char* s = "invalid";
	size_t i;
	for (i = 0; i != h->u.file.path_len; ++i)
	{
	  if (h->u.file.path[i] == 0)
	  {
	    s = (const char*)h->u.file.path;
	    break ;
	  }
	}
	printf(".path          : %s\n", s);
	break ;
      }

    default:
      {
	break ;
      }
    }

    printf("\n");
  }

  err = 0;

 on_error_1:
  efpak_istream_fini(&is);
 on_error_0:
  return err;
}

static int do_create(int ac, const char** av)
{
  const char* const path = av[2];
  struct stat st;
  efpak_ostream_t os;

  if (ac != 3) return -1;

  /* must not already exist */
  errno = 0;
  if (stat(path, &st) == 0) errno = 0;
  if (errno != ENOENT) return -1;

  if (efpak_ostream_init_with_file(&os, path)) return -1;
  efpak_ostream_fini(&os);
  return 0;
}

static int do_add_disk(int ac, const char** av)
{
  return -1;
}

static int do_add_part(int ac, const char** av)
{
  return -1;
}

static int do_add_file(int ac, const char** av)
{
  const char* const efpak_path = av[2];
  const char* const src_path = av[3];
  const char* const dst_path = av[4];

  efpak_ostream_t os;
  int err = -1;

  if (ac != 5) goto on_error_0;
  if (efpak_ostream_init_with_file(&os, efpak_path)) goto on_error_0;
  if (efpak_ostream_add_file(&os, src_path, dst_path)) goto on_error_1;
  err = 0;

 on_error_1:
  efpak_ostream_fini(&os);
 on_error_0:
  return err;
}

static int do_get_disk(int ac, const char** av)
{
  return -1;
}

static int do_get_part(int ac, const char** av)
{
  return -1;
}

static int do_get_file(int ac, const char** av)
{
  return -1;
}

static int do_update_disk(int ac, const char** av)
{
  return -1;
}

static int do_help(int ac, const char** av)
{
  const char* const usage =
    ". package info: \n"
    " efpak list_headers \n"
    "\n"
    ". package creation: \n"
    " efpak create efpak_path \n"
    "\n"
    ". adding contents: \n"
    " efpak add_disk efpak_path disk_path \n"
    " efpak add_part efpak_path {boot,root,app} part_path \n"
    " efpak add_file efpak_path \n"
    "\n"
    ". extracting contents: \n"
    " efpak get_disk efpak_path disk_path \n"
    " efpak get_part efpak_path {boot,root,app} part_path \n"
    " efpak get_file efpak_path file_name file_path \n"
    "\n"
    ". disk update: \n"
    " efpak update_disk efpak_path {root,disk_path} \n"
    ;

  printf("%s\n", usage);

  return -1;
}

int main(int ac, const char** av)
{
  static const struct
  {
    const char* name;
    int (*fn)(int, const char**);
  } ops[] =
  {
    { "list_headers", do_list_headers },
    { "create", do_create },
    { "add_disk", do_add_disk },
    { "add_part", do_add_part },
    { "add_file", do_add_file },
    { "get_disk", do_get_disk },
    { "get_part", do_get_part },
    { "get_file", do_get_file },
    { "update_disk", do_update_disk },
    { "help", do_help }
  };

  static const size_t n = sizeof(ops) / sizeof(ops[0]);
  size_t i;
  int err;

  if (ac <= 2)
  {
    i = n - 1;
    goto on_help;
  }

  for (i = 0; i != n; ++i)
  {
    if (strcmp(ops[i].name, av[1]) == 0) break ;
  }
  if (i == n) i = n - 1;

 on_help:
  err = ops[i].fn(ac, av);
  if (err) printf("failure\n");
  else printf("success\n");
  return err;
}
