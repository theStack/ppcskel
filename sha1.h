#ifndef __SHA1_H__
#define __SHA1_H__

#include "types.h"

#define SHA_CMD_FLAG_EXEC (1<<31)
#define SHA_CMD_FLAG_IRQ  (1<<30)
#define SHA_CMD_FLAG_ERR  (1<<29)
#define SHA_CMD_AREA_BLOCK ((1<<10) - 1)

typedef struct {
    unsigned long state[5];
    unsigned long count[2];
    unsigned char buffer[64];
} SHA1_CTX;

typedef u32 sha1[5];

void SHA1Transform(unsigned long state[5], unsigned char buffer[64]);
void SHA1Transforml(unsigned long state[5], unsigned char buffer[64], u32 len);
void SHA1Init(SHA1_CTX* context);
void SHA1Update(SHA1_CTX* context, unsigned char* data, unsigned int len);
void SHA1Final(unsigned char digest[20], SHA1_CTX* context);

void SHA1(unsigned char *ptr, unsigned int size, unsigned char *outbuf);

void SHA1TestCases(void);

#endif
