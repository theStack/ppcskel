#ifndef __CRYPTO_H__
#define __CRYPTO_H__

#include "types.h"

#define SHA_CMD_FLAG_EXEC (1<<31)
#define SHA_CMD_FLAG_IRQ  (1<<30)
#define SHA_CMD_FLAG_ERR  (1<<29)
#define SHA_CMD_AREA_BLOCK ((1<<10) - 1)

void sha1_reset(void);
void sha1_showresult(void);
void sha1_hash(u8 *src, u32 num_blocks);

#endif

