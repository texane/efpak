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
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <linux/fs.h>
#include <linux/hdreg.h>
#include <linux/blkpg.h>
#include "disk.h"
#include "libefpak.h" 


/* #ifdef DISK_UNIT */
#if 1
#include <stdio.h>
#define PERROR()			\
do {					\
  printf("[!] %u\n", __LINE__);		\
  fflush(stdout);			\
} while (0)
#elif 0
#include "log.h"
#define PERROR() LOG_ERROR("")
#else
#define PERROR()
#endif


/* generic raw disk based accesses */

static const char* get_root_dev_name(char* s, size_t n)
{
  /* find the block device the root is mount on */

#if 0 /* iterative method */

  struct stat st_root;
  struct stat st_dent;
  char* p;
  int err = -1;
  DIR* dir;

  if (stat("/", &st_root)) goto on_error_0;

  dir = opendir("/dev");
  if (dir == NULL) goto on_error_0;

  strcpy(s, "/dev/");
  p = s + sizeof("/dev/") - 1;
  n -= sizeof("/dev/") - 1;

  while (err == -1)
  {
    struct dirent* const de = readdir(dir);

    if (de == NULL) break ;

    strncpy(p, de->d_name, n);
    p[n - 1] = 0;

    if (lstat(s, &st_dent)) continue ;

    if (S_ISBLK(st_dent.st_mode) == 0) continue ;

    if (major(st_dent.st_rdev) != major(st_root.st_dev)) continue ;
    if (minor(st_dent.st_rdev) != minor(st_root.st_dev)) continue ;

    err = 0;
  }

  closedir(dir);
 on_error_0:
  return err == -1 ? NULL : p;

#else /* fast method */

  const ssize_t res = readlink("/dev/root", s, n);
  if (res < 2) return NULL;
  /* skip pX partition suffix */
  s[res - 2] = 0;
  return s;

#endif /* iterative method */
}

static int get_chs_geom(int fd, size_t* chs, size_t sector_count)
{
  struct hd_geometry geom;

  if (ioctl(fd, HDIO_GETGEO, &geom))
  {
    geom.heads = 255;
    geom.sectors = 63;
    geom.cylinders = sector_count / (255 * 63);
  }

  chs[0] = (size_t)geom.cylinders;
  chs[1] = (size_t)geom.heads;
  chs[2] = (size_t)geom.sectors;

  return 0;
}

static int get_block_size(int fd, uint64_t* size)
{
  /* BLKPBSZGET */
  unsigned int tmp;
  if (ioctl(fd, BLKPBSZGET, &tmp)) return -1;
  *size = (uint64_t)tmp;
  return 0;
}

static int get_dev_size(int fd, uint64_t* size)
{
  /* BLKGETSIZE defined linux/fs.h */
  /* note: the size is returned in bytes / 512 */

  long arg;
  if (ioctl(fd, BLKGETSIZE, &arg)) return -1;
  *size = (uint64_t)arg;
  return 0;
}

static int read_size_t(int fd, uint64_t* x)
{
  char buf[32];
  const ssize_t n = read(fd, buf, sizeof(buf) - 1);

  if ((n <= 0) || (n == (sizeof(buf) - 1))) return -1;
  buf[n] = 0;
  *x = (uint64_t)atoll(buf);
  return 0;
}

static char* make_part_sysfs_path(const char* disk_name, size_t i)
{
  static char path[512];
  char buf[4];

  strcpy(path, "/sys/class/block/");
  strcat(path, disk_name);
  strcat(path, "p");
  buf[0] = '0' + i;
  buf[1] = 0;
  strcat(path, buf);

  return path;
}

static unsigned int does_part_exist(const char* name, size_t i)
{
  const char* const path = make_part_sysfs_path(name, i);
  struct stat st;
  return stat(path, &st) == 0;
}

static int get_part_off(const char* name, size_t i, uint64_t* off)
{
  /* offset returned in block size */

  char* path;
  int fd;
  int err;

  path = make_part_sysfs_path(name, i);
  strcat(path, "/start");

  fd = open(path, O_RDONLY);
  if (fd == -1) return -1;

  err = read_size_t(fd, off);
  close(fd);

  return err;
}

static int get_part_size(const char* name, size_t i, uint64_t* off)
{
  /* size returned in block size */

  char* path;
  int fd;
  int err;

  path = make_part_sysfs_path(name, i);
  strcat(path, "/size");

  fd = open(path, O_RDONLY);
  if (fd == -1) return -1;

  err = read_size_t(fd, off);
  close(fd);

  return err;
}

static int disk_open(disk_handle_t* disk)
{
  const char* const dev_path = disk->dev_path;
  const char* const dev_name = disk->dev_name;

  uint64_t dev_size;
  struct stat st;
  size_t i;

  disk->fd = open(dev_path, O_RDWR | O_LARGEFILE | O_SYNC);
  if (disk->fd == -1)
  {
    PERROR();
    goto on_error_0;
  }

  if (fstat(disk->fd, &st))
  {
    PERROR();
    goto on_error_1;
  }
  disk->dev_maj = major(st.st_rdev);

  if (get_block_size(disk->fd, &disk->block_size))
  {
    PERROR();
    goto on_error_1;
  }

  /* it is assumed block_size == 512 */
  if (disk->block_size != DISK_BLOCK_SIZE)
  {
    PERROR();
    goto on_error_1;
  }

  /* note: the size is returned in bytes /512 */
  if (get_dev_size(disk->fd, &dev_size))
  {
    PERROR();
    goto on_error_1;
  }
  disk->block_count = (dev_size / disk->block_size) * 512;

  if (get_chs_geom(disk->fd, disk->chs, (size_t)disk->block_count))
  {
    PERROR();
    goto on_error_1;
  }

  for (i = 0; i < DISK_MAX_PART_COUNT; ++i)
  {
    const size_t part_id = i + 1;

    if (does_part_exist(dev_name, part_id) == 0) break ;

    if (get_part_off(dev_name, part_id, &disk->part_off[i]))
    {
      PERROR();
      goto on_error_1;
    }

    if (get_part_size(dev_name, part_id, &disk->part_size[i]))
    {
      PERROR();
      goto on_error_1;
    }
  }
  disk->part_count = i;

  /* success */
  return 0;

 on_error_1:
  close(disk->fd);
 on_error_0:
  return -1;
}

static void prepend_slash_dev(disk_handle_t* disk)
{
  const size_t pre_size = sizeof("/dev/") - 1;
  memcpy(disk->dev_path, "/dev/", pre_size);
  disk->dev_name = disk->dev_path + pre_size;
  disk->name_size = sizeof(disk->dev_path) - pre_size;
}

int disk_open_root(disk_handle_t* disk)
{
  prepend_slash_dev(disk);
  if (get_root_dev_name(disk->dev_name, disk->name_size) == NULL) return -1;
  return disk_open(disk);
}

int disk_open_dev(disk_handle_t* disk, const char* name)
{
  prepend_slash_dev(disk);
  if (strlen(name) >= disk->name_size) return -1;
  strcpy(disk->dev_name, name);
  return disk_open(disk);
}

void disk_close(disk_handle_t* disk)
{
  close(disk->fd);
}

int disk_seek(disk_handle_t* disk, size_t off)
{
  const off64_t off64 = (off64_t)off * (off64_t)disk->block_size;
  if (lseek64(disk->fd, off64, SEEK_SET) != off64) return -1;
  return 0;
}

int disk_write
(disk_handle_t* disk, size_t off, size_t size, const uint8_t* buf)
{
  /* assume size * disk->block_size does not overflow */
  if (disk_seek(disk, off) == -1) return -1;
  size *= disk->block_size;

  if (write(disk->fd, buf, size) != (ssize_t)size) return -1;
  return 0;
}

int disk_read
(disk_handle_t* disk, size_t off, size_t size, uint8_t* buf)
{
  /* assume size * disk->block_size does not overflow */
  if (disk_seek(disk, off) == -1) return -1;
  size *= disk->block_size;
  if (read(disk->fd, buf, size) != (ssize_t)size) return -1;
  return 0;
}

#if 0 /* dance configuration */

typedef struct conf_header
{
  /* stored in big endian format */
#define CONF_HEADER_MAGIC 0xda5cac5e
  uint32_t magic;
  /* total size, header included */
  uint32_t size;
  uint32_t vers;
} __attribute__((packed)) conf_header_t;

static uint32_t get_uint32_be(const uint8_t* buf)
{
  uint32_t x;

  x = ((uint32_t)buf[0]) << 24;
  x |= ((uint32_t)buf[1]) << 16;
  x |= ((uint32_t)buf[2]) << 8;
  x |= ((uint32_t)buf[3]) << 0;

  return x;
}

static size_t get_conf_size(const conf_header_t* h)
{
  return (size_t)get_uint32_be((const uint8_t*)&h->size);
}

static uint32_t get_conf_magic(const conf_header_t* h)
{
  return get_uint32_be((const uint8_t*)&h->magic);
}

static unsigned int is_conf_magic(const conf_header_t* h)
{
  return get_conf_magic(h) == CONF_HEADER_MAGIC;
}

#endif /* dance configuration */


/* mbr structures */
/* http://en.wikipedia.org/wiki/Master_boot_record */

typedef struct
{
  /* little endian encoding */
  uint8_t status;
  uint8_t first_chs[3];
  uint8_t type;
  uint8_t last_chs[3];
  uint32_t first_lba;
  uint32_t sector_count;
} __attribute__((packed)) mbe_t;

typedef struct
{
  uint8_t code[446];
#define MBR_ENTRY_COUNT 4
  mbe_t entries[MBR_ENTRY_COUNT];
#define MBR_MAGIC_00 0x55
#define MBR_MAGIC_01 0xaa
  uint8_t magic[2];
} __attribute__((packed)) mbr_t;

static unsigned int is_mbr_magic(const mbr_t* mbr)
{
  const uint8_t* const magic = mbr->magic;
  return (magic[0] == MBR_MAGIC_00) && (magic[1] == MBR_MAGIC_01);
}

static unsigned int is_mbe_valid(const mbe_t* e)
{
  if (e->status & (0x80 - 1)) return 0;
  return (e->type == 0x0c) || (e->type == 0x83);
}

static unsigned int is_mbe_active(const mbe_t* e)
{
  return e->status & (1 << 7);
}

static size_t find_active_mbe(const mbr_t* mbr)
{
  size_t i;

  for (i = 0; i != MBR_ENTRY_COUNT; ++i)
  {
    if (is_mbe_active(&mbr->entries[i])) break ;
  }

  return i;
}

static void put_uint32_le(uint8_t* buf, uint32_t x)
{
  buf[0] = (uint8_t)(x >> 0);
  buf[1] = (uint8_t)(x >> 8);
  buf[2] = (uint8_t)(x >> 16);
  buf[3] = (uint8_t)(x >> 24);
}

__attribute__((unused))
static uint32_t get_uint32_le(const uint8_t* buf)
{
  uint32_t x;

  x = ((uint32_t)buf[0]) << 0;
  x |= ((uint32_t)buf[1]) << 8;
  x |= ((uint32_t)buf[2]) << 16;
  x |= ((uint32_t)buf[3]) << 24;

  return x;
}

__attribute__((unused))
static size_t chs_to_lba
(const size_t* geom, const uint8_t* chs)
{
  /* http://en.wikipedia.org/wiki/Logical_block_addressing */

  const size_t hpc = geom[1];
  const size_t spt = geom[2];

  const size_t hi = (((size_t)chs[1]) << 2) & ~((1 << 8) - 1);
  const size_t c = (size_t)(hi | (size_t)chs[2]);
  const size_t h = (size_t)chs[0];
  const size_t s = (size_t)(chs[1] & ((1 << 6) - 1));

  const size_t lba = (c * hpc + h) * spt + s - 1;

  return lba;
}

static void lba_to_chs
(const size_t* geom, size_t lba, uint8_t* chs)
{
  const size_t hpc = geom[1];
  const size_t spt = geom[2];

  const size_t c = lba / (spt * hpc);
  const size_t h = (lba / spt) % hpc;
  const size_t s = (lba % spt) + 1;

  chs[0] = (uint8_t)h;
  chs[1] = (uint8_t)(s | ((c >> 2) & ~((1 << 6) - 1)));
  chs[2] = (uint8_t)c;
}

static void set_mbe_addr
(mbe_t* e, const size_t* chs, size_t off, size_t size)
{
  /* off and size in sectors */

  lba_to_chs(chs, off, e->first_chs);
  lba_to_chs(chs, off + size - 1, e->last_chs);

  put_uint32_le((uint8_t*)&e->first_lba, (uint32_t)off);
  put_uint32_le((uint8_t*)&e->sector_count, (uint32_t)size);
}

static void set_mbe_type(mbe_t* e, uint8_t type)
{
  e->type = type;
}

static void set_mbe_status(mbe_t* e, uint8_t status)
{
  e->status = status;
}

static void get_mbe_addr
(const mbe_t* e, const size_t* chs, size_t* off, size_t* size)
{
  /* off and size in sectors */

#if 0
  const size_t first_lba = (size_t)chs_to_lba(chs, e->first_chs);
  const size_t last_lba = (size_t)chs_to_lba(chs, e->last_chs);
  *off = first_lba;
  *size = 1 + last_lba - first_lba;
#else
  *off = (size_t)get_uint32_le((const uint8_t*)&e->first_lba);
  *size = (size_t)get_uint32_le((const uint8_t*)&e->sector_count);
#endif
}


/* disk update routines */

static int disk_write_with_efpak
(disk_handle_t* disk, efpak_istream_t* is, size_t off, size_t size)
{
  const uint8_t* p;
  size_t i;
  size_t n;

  for (i = 0; i != size; i += n, off += n)
  {
    if (size != (size_t)-1) n = (size - i) * DISK_BLOCK_SIZE;
    else n = (size_t)-1;

    if (efpak_istream_next(is, &p, &n))
    {
      PERROR();
      return -1;
    }

    if (n == 0) break ;

    n /= DISK_BLOCK_SIZE;
    if (disk_write(disk, off, n, p))
    {
      PERROR();
      return -1;
    }
  }

  return 0;
}

static int file_write_with_efpak
(int fd, efpak_istream_t* is, size_t size)
{
  const uint8_t* p;
  size_t i;
  size_t n;

  for (i = 0; i != size; i += n)
  {
    if (size != (size_t)-1) n = size - i;
    else n = (size_t)-1;

    if (efpak_istream_next(is, &p, &n))
    {
      PERROR();
      return -1;
    }

    if (n == 0) break ;

    if (write(fd, p, n) != (ssize_t)n)
    {
      PERROR();
      return -1;
    }
  }

  return 0;
}

typedef struct install_handle
{
  efpak_istream_t* is;
  disk_handle_t* disk;
  const efpak_header_t* h;

#define INSTALL_FLAG_MBR (1 << 0)
#define INSTALL_FLAG_LAY (1 << 1)
  uint32_t flags;

  mbr_t mbr;

  /* area offset and size in sectors */
  size_t area_off[3];
  size_t area_size[3];

  /* partition index in mbr */
  size_t mbr_index[3];

  /* partition offset and size in sectors */
  size_t part_off[3];
  size_t part_size[3];

} install_handle_t;

static int install_init
(install_handle_t* inst, disk_handle_t* disk, efpak_istream_t* is)
{
  inst->disk = disk;
  inst->is = is;
  inst->flags = 0;
  return 0;
}

static int install_get_part_layout(install_handle_t* inst)
{
  /* get the current partitioning layout */

  const size_t max_disk_size = UINT32_MAX / DISK_BLOCK_SIZE;
  const size_t empty_size = (512 + 2 * 1024 * 1024) / DISK_BLOCK_SIZE;
  const size_t boot_size = (2 * 256 * 1024 * 1024) / DISK_BLOCK_SIZE;
  const size_t root_size = (2 * 512 * 1024 * 1024) / DISK_BLOCK_SIZE;
  const size_t app_size = (2 * 512 * 1024 * 1024) / DISK_BLOCK_SIZE;

  size_t disk_size;
  size_t boot_index;
  size_t i;

  /* already got */
  if (inst->flags & INSTALL_FLAG_LAY) goto on_success;
  inst->flags |= INSTALL_FLAG_LAY;

  /* get disk size, trunc to 4GB. */
  disk_size = inst->disk->block_count;
  if (disk_size > max_disk_size) disk_size = max_disk_size;

  /* find boot partition. deduce root and app info. */

  boot_index = find_active_mbe(&inst->mbr);
  /* if (boot_index == MBR_ENTRY_COUNT) goto on_error; */
  if (boot_index > 1) goto on_error;

  for (i = 0; i != 3; ++i)
  {
    const mbe_t* const mbe = &inst->mbr.entries[boot_index + i];

    inst->mbr_index[i] = boot_index + i;

    if (is_mbe_valid(mbe) == 0)
    {
      inst->part_size[i] = 0;
      continue ;
    }

    get_mbe_addr(mbe, inst->disk->chs, &inst->part_off[i], &inst->part_size[i]);
  }

  /* area bases and sizes */
  /* refer to firmware disk documentation */
  inst->area_off[0] = empty_size;
  inst->area_size[0] = boot_size;
  inst->area_off[1] = inst->area_off[0] + boot_size;
  inst->area_size[1] = root_size;
  inst->area_off[2] = inst->area_off[1] + root_size;
  inst->area_size[2] = app_size;
  if ((inst->area_off[2] + inst->area_size[2]) > disk_size) goto on_error;

 on_success:
  return 0;

 on_error:
  return -1;
}

static int mount_part
(
 install_handle_t* inst,
 int dev_min,
 const char* mnt_path, unsigned long mnt_flags,
 const char* fs_name,
 const char* vol_name,
 uint64_t off, uint64_t size
)
{
  disk_handle_t* const disk = inst->disk;

  int err = -1;
  const size_t dev_path_len = strlen(disk->dev_path);
  const dev_t dev_dev = makedev(disk->dev_maj, dev_min);
  struct blkpg_ioctl_arg blkpg_arg;
  struct blkpg_partition blkpg_part;

  /* add a new partition and mount it */

  disk->dev_path[dev_path_len + 0] = 'p';
  disk->dev_path[dev_path_len + 1] = (char)'0' + (char)dev_min;
  disk->dev_path[dev_path_len + 2] = 0;

  blkpg_part.start = (long long)(off * DISK_BLOCK_SIZE);
  blkpg_part.length = (long long)(size * DISK_BLOCK_SIZE);
  blkpg_part.pno = dev_min;
  strcpy(blkpg_part.devname, disk->dev_path);
  strcpy(blkpg_part.volname, vol_name);

  blkpg_arg.op = BLKPG_ADD_PARTITION;
  blkpg_arg.flags = 0;
  blkpg_arg.datalen = sizeof(blkpg_part);
  blkpg_arg.data = (void*)&blkpg_part;

  errno = 0;
  if (ioctl(disk->fd, BLKPG, &blkpg_arg))
  {
    PERROR();
    goto on_error_0;
  }

  errno = 0;
  if (mknod(disk->dev_path, S_IRWXU | S_IFBLK, dev_dev))
  {
    if (errno != EEXIST)
    {
      PERROR();
      goto on_error_1;
    }
  }

  errno = 0;
  if (mkdir(mnt_path, S_IRWXU))
  {
    if (errno != EEXIST)
    {
      PERROR();
      goto on_error_2;
    }
  }

  /* umount if already mounted */
  umount(mnt_path);

  errno = 0;
  if (mount(disk->dev_path, mnt_path, fs_name, mnt_flags, NULL))
  {
    PERROR();
    goto on_error_3;
  }

  err = 0;
  goto on_success;

 on_error_3:
  rmdir(mnt_path);
 on_error_2:
  unlink(disk->dev_path);
 on_error_1:
  blkpg_arg.op = BLKPG_DEL_PARTITION;
  ioctl(disk->fd, BLKPG, &blkpg_arg);
 on_error_0:
 on_success:
  disk->dev_path[dev_path_len] = 0;
  return err;
}

static int install_part(install_handle_t* inst)
{
  static const size_t mbr_nblk = sizeof(mbr_t) / DISK_BLOCK_SIZE;

  disk_handle_t* const disk = inst->disk;
  efpak_istream_t* const is = inst->is;
  const efpak_header_t* const h = inst->h;
  const char* mnt_path;
  const char* vol_name;
  const char* fs_name;
  int dev_min;
  unsigned long mnt_flags;
  int err;
  size_t i;
  size_t off;
  size_t size;
  mbe_t* mbe;

  if ((inst->flags & INSTALL_FLAG_MBR) == 0)
  {
    inst->flags |= INSTALL_FLAG_MBR;

    /* get mbr from disk */

    if (disk_read(inst->disk, 0, mbr_nblk, (uint8_t*)&inst->mbr))
    {
      PERROR();
      goto on_error;
    }

    if (!is_mbr_magic(&inst->mbr))
    {
      PERROR();
      goto on_error;
    }

    if (install_get_part_layout(inst))
    {
      PERROR();
      goto on_error;
    }
  }

  /* get partition new layout, ie. where to store in area */

  switch ((efpak_partid_t)inst->h->u.part.id)
  {
  case EFPAK_PARTID_BOOT:
    {
      i = 0;
      vol_name = "new_boot";
      mnt_path = "/tmp/new_boot";
      dev_min = 5;
      fs_name = "vfat";
      mnt_flags = 0;
      break ;
    }

  case EFPAK_PARTID_ROOT:
    {
      i = 1;
      vol_name = "new_root";
      mnt_path = "/tmp/new_root";
      dev_min = 6;
      fs_name = "squashfs";
      mnt_flags = MS_RDONLY;
      break ;
    }

  case EFPAK_PARTID_APP:
    {
      i = 2;
      vol_name = "new_app";
      mnt_path = "/tmp/new_app";
      dev_min = 7;
      fs_name = "ext3";
      mnt_flags = 0;
      break ;
    }

  default: goto on_error; break ;
  }

  off = inst->area_off[i];
  if (off == inst->part_off[i]) off += inst->area_size[i] / 2;

  /* check uncompressed size wont overwrite next area */

  if (h->raw_data_size > ((uint64_t)UINT32_MAX - DISK_BLOCK_SIZE))
  {
    PERROR();
    goto on_error;
  }

  size = (size_t)inst->h->raw_data_size;
  if (size % DISK_BLOCK_SIZE) size += DISK_BLOCK_SIZE;
  size /= DISK_BLOCK_SIZE;
  if ((off + size) > (inst->area_off[i] + inst->area_size[i]))
  {
    PERROR();
    goto on_error;
  }

  /* write the new partition contents */

  if (disk_write_with_efpak(disk, is, off, (size_t)-1))
  {
    PERROR();
    goto on_error;
  }

  /* update mbr */

  mbe = &inst->mbr.entries[inst->mbr_index[i]];
  set_mbe_addr(mbe, inst->disk->chs, off, size);

  if (i == 2)
  {
    /* mbe may not exist before */
    set_mbe_status(mbe, 0x00);
    set_mbe_type(mbe, 0x83);
  }

  /* mount new partition in /tmp/new_xxx */

  err = mount_part
    (inst, dev_min, mnt_path, mnt_flags, fs_name, vol_name, off, size);
  if (err)
  {
    PERROR();
    goto on_error;
  }

  return 0;

 on_error:
  return -1;
}

static int install_disk(install_handle_t* inst)
{
  disk_handle_t* const disk = inst->disk;
  efpak_istream_t* const is = inst->is;
  const uint8_t* data;
  size_t size;
  size_t i;

  /* it is invalid to install a disk twice, or with a partion */
  if (inst->flags & INSTALL_FLAG_MBR)
  {
    PERROR();
    goto on_error;
  }

  inst->flags |= INSTALL_FLAG_MBR;

  /* get mbr from new disk image */

  size = sizeof(mbr_t);
  if (efpak_istream_next(is, &data, &size))
  {
    PERROR();
    goto on_error;
  }

  if (size != sizeof(mbr_t))
  {
    PERROR();
    goto on_error;
  }

  if (!is_mbr_magic((const mbr_t*)data))
  {
    PERROR();
    goto on_error;
  }

  memcpy(&inst->mbr, data, sizeof(mbr_t));

  if (install_get_part_layout(inst))
  {
    PERROR();
    goto on_error;
  }

  /* install empty partition, as may be needed by grub */
  /* empty partition starts from mbr end to boot */

  if (inst->part_off[0] != 1)
  {
    if (efpak_istream_seek(is, DISK_BLOCK_SIZE))
    {
      PERROR();
      goto on_error;
    }

    if (disk_write_with_efpak(disk, is, 1, inst->part_off[0] - 1))
    {
      PERROR();
      goto on_error;
    }
  }

  /* install boot, root and app partitions */

  for (i = 0; i != 3; ++i)
  {
    mbe_t* const mbe = &inst->mbr.entries[inst->mbr_index[i]];

    if (inst->part_size[i] == 0) continue ;

    if (efpak_istream_seek(is, inst->part_off[i] * DISK_BLOCK_SIZE))
    {
      PERROR();
      goto on_error;
    }

    if (disk_write_with_efpak(disk, is, inst->area_off[i], inst->part_size[i]))
    {
      PERROR();
      goto on_error;
    }

    set_mbe_addr(mbe, inst->disk->chs, inst->area_off[i], inst->part_size[i]);
  }

  return 0;

 on_error:
  return -1;
}

static int install_file(install_handle_t* inst)
{
  const efpak_header_t* const h = inst->h;
  efpak_istream_t* const is = inst->is;
  const char* const file_path = (const char*)h->u.file.path;
  const size_t path_len = (size_t)h->u.file.path_len;
  size_t size;
  size_t i;
  int err = -1;
  int fd;
  char dir_path[256];

  /* check file size */
  if (h->raw_data_size > (uint64_t)UINT32_MAX) goto on_error_0;
  size = (size_t)h->raw_data_size;

  /* must start with a slash */
  if (path_len == 0) goto on_error_0;
  if (file_path[0] != '/') goto on_error_0;

  if (path_len > sizeof(dir_path)) goto on_error_0;

  /* create directories along the path */
  dir_path[0] = '/';
  for (i = 1; 1; ++i)
  {
    /* not zero terminated */
    if (i == path_len) goto on_error_0;

    dir_path[i] = file_path[i];
    if (file_path[i] == 0) break ;

    if (file_path[i] != '/') continue ;

    /* create directory if does not exist */
    dir_path[i] = 0;
    errno = 0;
    if (mkdir(dir_path, 0755))
    {
      if (errno != EEXIST) goto on_error_0;
    }
    dir_path[i] = '/';
  }

  /* create the file */
  fd = open(file_path, O_RDWR | O_TRUNC | O_CREAT, 0755);
  if (fd == -1) goto on_error_0;
  if (file_write_with_efpak(fd, is, size)) goto on_error_1;

  err = 0;
 on_error_1:
  close(fd);
 on_error_0:
  return err;
}

static int install_efpak(install_handle_t* inst)
{
  /* to increase safety, the mbr is updated only */
  /* if all the previous operations succeeded */

  static const size_t mbr_nblk = sizeof(mbr_t) / DISK_BLOCK_SIZE;
  int err;

  while (1)
  {
    err = efpak_istream_next_block(inst->is, &inst->h);
    if (err) goto on_error;

    if (inst->h == NULL) break ;

    err = efpak_istream_start_block(inst->is);
    if (err) goto on_error;

    switch (inst->h->type)
    {
    case EFPAK_BTYPE_FORMAT:
      {
	break ;
      }

    case EFPAK_BTYPE_DISK:
      {
	err = install_disk(inst);
	break ;
      }

    case EFPAK_BTYPE_PART:
      {
	err = install_part(inst);
	break ;
      }

    case EFPAK_BTYPE_FILE:
      {
	err = install_file(inst);
	break ;
      }

    default:
      {
	err = -1;
	break ;
      }
    }

    efpak_istream_end_block(inst->is);

    if (err)
    {
      PERROR();
      goto on_error;
    }
  }

  /* reset error */
  err = -1;

  if (inst->flags & INSTALL_FLAG_MBR)
  {
    /* commit the mbr */

    if (disk_write(inst->disk, 0, mbr_nblk, (const uint8_t*)&inst->mbr))
    {
      PERROR();
      goto on_error;
    }
  }

  err = 0;

 on_error:
  return err;
}

int disk_install_with_efpak(disk_handle_t* disk, efpak_istream_t* is)
{
  install_handle_t inst;
  install_init(&inst, disk, is);
  return install_efpak(&inst);
}
