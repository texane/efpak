#ifndef EFPAK_H_INCLUDED
#define EFPAK_H_INCLUDED


#include <stddef.h>
#include <stdint.h>


/* data words are stored in little endian format */

/* block types */
typedef enum efpak_btype
{
  EFPAK_BTYPE_FORMAT = 0,
  EFPAK_BTYPE_DISK,
  EFPAK_BTYPE_PART,
  EFPAK_BTYPE_FILE,
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


/* information about efpak itself */
typedef struct efpak_format_header
{
  /* must be EFPK */
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
  uint8_t id;

  /* partition offset and size */
  uint64_t off;
  uint64_t size;
} __attribute__((packed)) efpak_part_header_t;


/* file block header */
typedef struct efpak_file_header
{
  uint16_t path_len;
  uint8_t path[1];
} __attribute__((packed)) efpak_file_header_t;


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

  /* compressed and raw block size in bytes, header included */
  /* if not compressed, comp_block_size == raw_block_size */
  uint64_t comp_block_size;
  uint64_t raw_block_size;

  union
  {
    efpak_format_header_t format;
    efpak_disk_header_t disk;
    efpak_part_header_t part;
    efpak_file_header_t file;
  } __attribute__((packed)) u;

} __attribute__((packed)) efpak_header_t;


#endif /* EFPAK_H_INCLUDED */
