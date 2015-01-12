#ifndef DECOMP_H_INCLUDED
#define DECOMP_H_INCLUDED


#include <stdint.h>
#include "zlib.h"


typedef struct
{
  z_stream z;

#define DECOMP_FLAG_EOI (1 << 1)
  uint32_t flags;

  uint8_t* obuf;
  size_t osize;

} decomp_handle_t;

int decomp_init(decomp_handle_t*, size_t);
int decomp_fini(decomp_handle_t*);
int decomp_reset(decomp_handle_t*);
int decomp_add_iblock(decomp_handle_t*, uint8_t*, size_t);
void decomp_set_eoi(decomp_handle_t*);
unsigned int decomp_is_done(decomp_handle_t*);
int decomp_set_single_iblock(decomp_handle_t*, uint8_t*, size_t);
int decomp_next_oblock(decomp_handle_t*, const uint8_t**, size_t*);


#endif /* DECOMP_H_INCLUDED */
