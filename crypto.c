#include "crypto.h"
#include "bootmii_ppc.h"
#include "string.h"

#define SHA_BASE	0x0d030000
#define	SHA_CTRL	(SHA_BASE+0x00)
#define SHA_SRC		(SHA_BASE+0x04)
#define SHA_H0		(SHA_BASE+0x08)
#define SHA_H1		(SHA_BASE+0x0c)
#define SHA_H2		(SHA_BASE+0x10)
#define SHA_H3		(SHA_BASE+0x14)
#define SHA_H4		(SHA_BASE+0x18)

#define SHA_CTRL_FLAG_EXEC (1<<31)
#define SHA_CTRL_FLAG_IRQ  (1<<30)
#define SHA_CTRL_FLAG_ERR  (1<<29)
#define SHA_CTRL_AREA_BLOCK ((1<<10) - 1)

#define SHA_DEBUG_STR "--- sha1 debug --- "

void sha1_reset(void)
{
	// reset sha-1 engine
	printf(SHA_DEBUG_STR "SHA_CTRL: %08X\n", read32(SHA_CTRL));
	write32(SHA_CTRL, read32(SHA_CTRL) & ~(SHA_CTRL_FLAG_EXEC));
	while ((read32(SHA_CTRL) & SHA_CTRL_FLAG_EXEC) != 0);
	printf(SHA_DEBUG_STR "SHA_CTRL: %08X\n", read32(SHA_CTRL));

	// initialization vector
	write32(SHA_H0, 0x67452301);
	write32(SHA_H1, 0xEFCDAB89);
	write32(SHA_H2, 0x98BADCFE);
	write32(SHA_H3, 0x10325476);
	write32(SHA_H4, 0xC3D2E1F0);
	sha1_showresult();
}

void sha1_showresult(void)
{
	printf(SHA_DEBUG_STR "SHA_H0: %08X\n", read32(SHA_H0));
	printf(SHA_DEBUG_STR "SHA_H1: %08X\n", read32(SHA_H1));
	printf(SHA_DEBUG_STR "SHA_H2: %08X\n", read32(SHA_H2));
	printf(SHA_DEBUG_STR "SHA_H3: %08X\n", read32(SHA_H3));
	printf(SHA_DEBUG_STR "SHA_H4: %08X\n", read32(SHA_H4));
}

void sha1_hash(u8 *src, u32 num_blocks)
{
	// assign block to local copy which is 64-byte aligned
	static u8 block[64] ALIGNED(64);
	memcpy(block, src, 64);
	printf("block[0]=%d, block[1]=%d, block[2]=%d, block[63]=%d\n",
		block[0], block[1], block[2], block[63]);

	// tell sha1 controller the block source address
	write32(SHA_SRC, virt_to_phys(block));
	
	printf(SHA_DEBUG_STR "local block array address: %08X\n", &(block[0]));
	printf(SHA_DEBUG_STR "sha1 controller block array address: %08X\n", read32(SHA_SRC));

	// tell sha1 controller number of blocks
	if (num_blocks != 0)
		num_blocks--;
	write32(SHA_CTRL, (read32(SHA_CTRL) & ~(SHA_CTRL_AREA_BLOCK)) | num_blocks);

	// fire up hashing and wait till its finished
	write32(SHA_CTRL, read32(SHA_CTRL) | SHA_CTRL_FLAG_EXEC);
	while (read32(SHA_CTRL) & SHA_CTRL_FLAG_EXEC);

	// show results
	sha1_showresult();

	printf(SHA_DEBUG_STR "SHA_CTRL: %08X\n", read32(SHA_CTRL));
}
