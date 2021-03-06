#ifndef EFPAK_H_INCLUDED
#define EFPAK_H_INCLUDED


#include <stdint.h>
#include <sys/types.h>
#include "zlib.h"


/* efpak file format */
/* words are stored in little endian */

/* block types */
typedef enum efpak_btype
{
  EFPAK_BTYPE_FORMAT = 0,
  EFPAK_BTYPE_DISK,
  EFPAK_BTYPE_PART,
  EFPAK_BTYPE_FILE,
  EFPAK_BTYPE_HOOK,
  EFPAK_BTYPE_INVALID
} efpak_btype_t;


/* block compression */
typedef enum efpak_bcomp
{
  EFPAK_BCOMP_NONE = 0,
  EFPAK_BCOMP_ZLIB,
  EFPAK_BCOMP_INVALID
} efpak_bcomp_t;


/* partition identifier */
typedef enum efpak_partid
{
  EFPAK_PARTID_BOOT = 0,
  EFPAK_PARTID_ROOT,
  EFPAK_PARTID_APP,
  EFPAK_PARTID_INVALID
} efpak_partid_t;


/* filesystem identifier */
typedef enum efpak_fsid
{
  EFPAK_FSID_VFAT = 0,
  EFPAK_FSID_SQUASH,
  EFPAK_FSID_EXT2,
  EFPAK_FSID_EXT3,
  EFPAK_FSID_INVALID
} efpak_fsid_t;


/* information about efpak itself */
typedef struct efpak_format_header
{
  /* must be EFPK */
#define EFPAK_FORMAT_SIGNATURE "EFPK"
  uint8_t signature[4];

  /* efpak format version */
  uint8_t vers;
} __attribute__((packed)) efpak_format_header_t;


/* disk block header */
typedef struct efpak_disk_header
{
  uint8_t dummy;
} __attribute__((packed)) efpak_disk_header_t;


/* partition block header */
typedef struct efpak_part_header
{
  /* one of efpak_partid_t */
  uint8_t part_id;
  /* one of efpak_fsid_t */
  uint8_t fs_id;
} __attribute__((packed)) efpak_part_header_t;


/* file block header */
typedef struct efpak_file_header
{
  /* must be a 0 terminated string. len includes 0. */
  uint16_t path_len;
  uint8_t path[1];
} __attribute__((packed)) efpak_file_header_t;


/* hook block header */
/* hooks are user defined commands to be executed at particular event during */
/* the extraction process. hooks drive the extraction process by exchanging */
/* information with the caller. for this purpose, the context in which they */
/* are called as well as the returned values are well defined. the calling */
/* conditions are specified by the user during the package creation and can */
/* be a combination of the following conditions: */
/* . now, when the hook block is interpreted */
/* . before object extraction */
/* . after object extraction (with a possible error code) */
/* . on completion (error or success) */
/* if the block data size is not zero, it contains the object to execute. */
/* if specified, a path is used to store the object. otherwise, a temporary */
/* file is used. if the block data size is zero, the path is used to locate */
/* the object to execute. */
typedef struct efpak_hook_header
{
  /* a hook can return one of the following values */
#define EFPAK_HOOK_CONTINUE 0
#define EFPAK_HOOK_SKIP_BLOCK 1
#define EFPAK_HOOK_STOP_SUCCESS 2
#define EFPAK_HOOK_STOP_ERROR 255
  /* any unknown code: stop with error */

#define EFPAK_HOOK_NOW (1 << 0)
#define EFPAK_HOOK_PREX (1 << 1)
#define EFPAK_HOOK_POSTX (1 << 2)
#define EFPAK_HOOK_COMPL (1 << 3)
#define EFPAK_HOOK_MBR (1 << 4)
  uint32_t when_flags;

#define EFPAK_HOOK_EXECVE (1 << 0)
  uint32_t exec_flags;

  /* must be a 0 terminated string. len includes 0. */
  uint16_t path_len;
  uint8_t path[1];
} __attribute__((packed)) efpak_hook_header_t;


/* generic block header */
typedef struct efpak_header
{
  /* versioning */
  uint8_t vers;

  /* block type, one of efpak_btype_t */
  uint8_t type;

  /* compression, one of efpak_bcomp_t */
  uint8_t comp;

  /* header size in bytes */
  uint64_t header_size;

  /* compressed and raw block data size in bytes */
  /* header is not included */
  /* if not compressed, comp_data_size == raw_data_size */
  uint64_t comp_data_size;
  uint64_t raw_data_size;

  union
  {
    efpak_format_header_t format;
    efpak_disk_header_t disk;
    efpak_part_header_t part;
    efpak_file_header_t file;
    efpak_hook_header_t hook;
    uint8_t per_type[1];
  } __attribute__((packed)) u;

} __attribute__((packed)) efpak_header_t;


/* input stream handling related types */

typedef struct efpak_inflate
{
  z_stream z;

#define EFPAK_INFLATE_FLAG_EOI (1 << 1)
  uint32_t flags;

  uint8_t* obuf;
  size_t osize;

} efpak_inflate_t;


typedef struct efpak_imem
{
  /* input block memory */

  /* common */
  const uint8_t* data;
  size_t size;
  size_t off;

  /* zlib memory specific */
  efpak_inflate_t inflate;
  const uint8_t* inflate_data;
  size_t inflate_size;

  int (*seek)(struct efpak_imem*, size_t);
  int (*next)(struct efpak_imem*, const uint8_t**, size_t*);
  void (*fini)(struct efpak_imem*);

} efpak_imem_t;


typedef struct efpak_istream
{
  /* input data buffer */
  const uint8_t* data;
  size_t size;

  /* offset in data buffer */
  size_t off;

  /* current block header */
  const efpak_header_t* header;

  /* start_block called */
  unsigned int is_in_block;

  /* current block memory */
  efpak_imem_t mem;

} efpak_istream_t;


typedef struct efpak_ostream
{
  /* output file descriptor */
  int fd;
} efpak_ostream_t;


/* input stream exported api */

int efpak_istream_init_with_file(efpak_istream_t*, const char*);
int efpak_istream_init_with_mem(efpak_istream_t*, const uint8_t*, size_t);
void efpak_istream_fini(efpak_istream_t*);
int efpak_istream_next_block(efpak_istream_t*, const efpak_header_t**);
int efpak_istream_start_block(efpak_istream_t*);
void efpak_istream_end_block(efpak_istream_t*);
int efpak_istream_seek(efpak_istream_t*, size_t);
int efpak_istream_next(efpak_istream_t*, const uint8_t**, size_t*);

int efpak_ostream_init_with_file(efpak_ostream_t*, const char*);
void efpak_ostream_fini(efpak_ostream_t*);
int efpak_ostream_add_disk(efpak_ostream_t*, const char*);
int efpak_ostream_add_part
(efpak_ostream_t*, const char*, efpak_partid_t, efpak_fsid_t);
int efpak_ostream_add_file(efpak_ostream_t*, const char*, const char*);
int efpak_ostream_add_hook
(efpak_ostream_t*, const char*, const char*, uint32_t, uint32_t);


#endif /* EFPAK_H_INCLUDED */
