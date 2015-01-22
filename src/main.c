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
#include <sys/mman.h>
#include "libefpak.h"
#include "disk.h"
#ifdef CONFIG_LIBDEEP
#include "libdeep.h"
#endif /* CONFIG_LIBDEEP */


#include <stdio.h>
#define PERROR()			\
do {					\
  printf("[!] %u\n", __LINE__);		\
  fflush(stdout);			\
} while (0)


static int do_list(int ac, const char** av)
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
	printf(".signature     : %c%c%c%c\n", s[0], s[1], s[2], s[3]);
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
  const char* const efpak_path = av[2];
  const char* const disk_path = av[3];

  efpak_ostream_t os;
  int err = -1;

  if (ac != 4) goto on_error_0;
  if (efpak_ostream_init_with_file(&os, efpak_path)) goto on_error_0;
  if (efpak_ostream_add_disk(&os, disk_path)) goto on_error_1;
  err = 0;

 on_error_1:
  efpak_ostream_fini(&os);
 on_error_0:
  return err;
}

static int do_add_part(int ac, const char** av)
{
  const char* const efpak_path = av[2];
  const char* const part_name = av[3];
  const char* const part_path = av[4];

  efpak_partid_t id;
  efpak_ostream_t os;
  int err = -1;

  if (ac != 5) goto on_error_0;

  if (strcmp(part_name, "boot") == 0) id = EFPAK_PARTID_BOOT;
  else if (strcmp(part_name, "root") == 0) id = EFPAK_PARTID_ROOT;
  else if (strcmp(part_name, "app") == 0) id = EFPAK_PARTID_APP;
  else goto on_error_0;

  if (efpak_ostream_init_with_file(&os, efpak_path)) goto on_error_0;
  if (efpak_ostream_add_part(&os, part_path, id)) goto on_error_1;
  err = 0;

 on_error_1:
  efpak_ostream_fini(&os);
 on_error_0:
  return err;
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

static int do_extract(int ac, const char** av)
{
  const char* const efpak_path = av[2];
  const char* const dir_path = av[3];

  char full_path[256];
  const efpak_header_t* h;
  efpak_istream_t is;
  int err = -1;
  int fd;
  unsigned int n = 0;
  size_t size;
  const uint8_t* data;

  if (ac != 4) goto on_error_0;

  errno = 0;
  if (mkdir(dir_path, 0755))
  {
    if (errno != EEXIST) goto on_error_0;
  }

  if (efpak_istream_init_with_file(&is, efpak_path)) goto on_error_0;

  while (1)
  {
    if (efpak_istream_next_block(&is, &h)) goto on_error_1;
    if (h == NULL) break ;
    if (h->type == EFPAK_BTYPE_FORMAT) continue ;

    /* create the file */
    snprintf(full_path, sizeof(full_path), "%s/%04x", dir_path, n++);
    full_path[sizeof(full_path) - 1] = 0;
    fd = open(full_path, O_RDWR | O_TRUNC | O_CREAT, 0755);
    if (fd == -1) goto on_error_1;

    /* extract the block */
    if (efpak_istream_start_block(&is))
    {
      close(fd);
      goto on_error_1;
    }

    while (1)
    {
      size = (size_t)-1;
      if (efpak_istream_next(&is, &data, &size))
      {
	close(fd);
	goto on_error_1;
      }

      if (size == 0) break ;

      if (write(fd, data, size) != (ssize_t)size)
      {
	close(fd);
	goto on_error_1;
      }
    }

    close(fd);

    efpak_istream_end_block(&is);
  }

  err = 0;

 on_error_1:
  efpak_istream_fini(&is);
 on_error_0:
  return err;
}

static int do_install(int ac, const char** av)
{
  const char* const efpak_path = av[2];
  const char* disk_name = av[3];
  int err = -1;
  efpak_istream_t is;
  disk_handle_t disk;

  if (ac != 4) goto on_error_0;

  if (efpak_istream_init_with_file(&is, efpak_path)) goto on_error_0;

  if (strcmp(disk_name, "root") == 0) err = disk_open_root(&disk);
  else err = disk_open_dev(&disk, disk_name);
  if (err) goto on_error_1;

  err = disk_install_with_efpak(&disk, &is);
  if (err) goto on_error_2;

 on_error_2:
  disk_close(&disk);
 on_error_1:
  efpak_istream_fini(&is);
 on_error_0:
  return err;
}

static int do_send(int ac, const char** av)
{
#ifdef CONFIG_LIBDEEP

  const char* const path = av[2];
  const char* addr = av[3];

  static const int timeout = 100000;

  deephandle_t dev;
  deepbindata_t data;
  char cmd[128];
  int err = -1;
  int fd;
  struct stat st;

  if (ac != 4) goto on_error_0;

  dev = deepdev_open((char*)addr);
  if (dev == BAD_HANDLE) goto on_error_0;

  deepdev_setparam(dev, "TIMEOUT", DDPAR(timeout));

  fd = open(path, O_RDONLY);
  if (fd == -1) goto on_error_1;

  if (fstat(fd, &st)) goto on_error_2;

  data.bufsize = st.st_size;
  data.datasize = st.st_size;
  data.datatype = BIN_8;
  data.databuf = mmap(NULL, data.bufsize, PROT_READ, MAP_SHARED, fd, 0);
  if (data.databuf == MAP_FAILED) goto on_error_2;

  snprintf(cmd, sizeof(cmd), "#*PROG");

  if (deepdev_bincmd(dev, cmd, &data) != DEEPDEV_OK) goto on_error_3;

  /* success */

  err = 0;

 on_error_3:
  munmap(data.databuf, data.bufsize);
 on_error_2:
  close(fd);
 on_error_1:
  deepdev_close(dev);
 on_error_0:
  return err;

#else

  return -1;

#endif /* CONFIG_LIBDEEP */
}

static int do_help(int ac, const char** av)
{
  const char* const usage =
    ". list package contents: \n"
    " efpak list \n"
    "\n"
    ". create a new package: \n"
    " efpak create efpak_path \n"
    "\n"
    ". adding contents: \n"
    " efpak add_disk efpak_path disk_path \n"
    " efpak add_part efpak_path {boot,root,app} part_path \n"
    " efpak add_file efpak_path \n"
    "\n"
    ". extracting contents: \n"
    " efpak extract efpak_path dest_dir \n"
    "\n"
    ". local disk install: \n"
    " efpak install efpak_path {root,disk_name(mmcblk0,sdd...)} \n"
    "\n"
    ". send package to remote device: \n"
    " efpak send efpak_path dev_addr \n"
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
    { "list", do_list },
    { "create", do_create },
    { "add_disk", do_add_disk },
    { "add_part", do_add_part },
    { "add_file", do_add_file },
    { "extract", do_extract },
    { "install", do_install },
    { "send", do_send },
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
