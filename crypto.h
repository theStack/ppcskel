#ifndef __CRYPTO_H__
#define __CRYPTO_H__

#include "types.h"

void sha1_reset(void);
void sha1_showresult(void);
void sha1_hash(u8 *src, u32 num_blocks);

#endif

