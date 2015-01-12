#ifndef EFPAK_H_INCLUDED
#define EFPAK_H_INCLUDED


#include <stddef.h>
#include <stdint.h>


/* data words are stored in little endian format */

/* block types */
typedef enum efpak_btype
{
  EFPAK_BTYPE_DISK = 0,
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


/* partition id */
typedef enum efpak_partid
{
  EFPAK_PARTID_BOOT = 0,
  EFPAK_PARTID_ROOT,
  EFPAK_PARTID_APP,
  EFPAK_PARTID_INVALID
} efpak_partid_t;


/* disk block */
typedef struct efpak_bdisk
{
  uint8_t dummy;
} __attribute__((packed)) efpak_bdisk_t;


/* partition block */
typedef struct efpak_bpart
{
  /* one of efpak_partid_t */
  uint8_t id;

  uint64_t off;
  uint64_t size;
} __attribute__((packed)) efpak_bpart_t;


/* file block */
typedef struct efpak_bfile
{
  uint8_t dummy;
} __attribute__((packed)) efpak_bfile_t;


/* common to all block headers */
typedef struct efpak_bcommon
{
  /* per type versioning */
  uint16_t vers;

  /* block type, one of efpak_btype_t */
  uint8_t type;

  /* compression, one of efpak_bcomp_t */
  uint8_t comp;

  /* total block size in bytes, without header */
  uint64_t size;

} __attribute__((packed)) efpak_bcommon_t;


/* every eefpak block starts with a header */
typedef struct efpak_bheader
{
  efpak_bcommon_t common;

  union
  {
    efpak_bdisk_t disk;
    efpak_bpart_t part;
    efpak_bfile_t file;

#define EFPAK_ALIGN_SIZE 512
    /* all the blocks sizes and offsets are aligned */
    /* on EFPAK_ALIGN_SIZE bytes to ease decompression */
    uint8_t pad[EFPAK_ALIGN_SIZE - sizeof(efpak_bcommon_t)];

  } __attribute__((packed)) u;

} __attribute__((packed)) efpak_bheader_t;


#endif /* EFPAK_H_INCLUDED */
