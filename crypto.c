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

void sha1_reset(void)
{
	// reset sha-1 engine
	write32(SHA_CTRL, read32(SHA_CTRL) & ~(SHA_CTRL_FLAG_EXEC));
	while ((read32(SHA_CTRL) & SHA_CTRL_FLAG_EXEC) != 0);

	// initialization vector
	write32(SHA_H0, 0x67452301);
	write32(SHA_H1, 0xEFCDAB89);
	write32(SHA_H2, 0x98BADCFE);
	write32(SHA_H3, 0x10325476);
	write32(SHA_H4, 0xC3D2E1F0);
}

void sha1_showresult(void)
{
	printf("SHA-1 controller says (H0|H1|H2|H3|H4): "
			"%08X|%08X|%08X|%08X|%08X\n",
			read32(SHA_H0), read32(SHA_H1),
			read32(SHA_H2), read32(SHA_H3),
			read32(SHA_H4));
}

void sha1_hash(u8 *src, u32 num_blocks)
{
	// assign block to local copy which is 64-byte aligned
	static u8 block[64] ALIGNED(64);
	memcpy(block, src, 64);

	// royal flush :)
	sync_after_write(block, 64*num_blocks);

	// tell sha1 controller the block source address
	write32(SHA_SRC, virt_to_phys(block));
	
	// tell sha1 controller number of blocks
	if (num_blocks != 0)
		num_blocks--;
	write32(SHA_CTRL, (read32(SHA_CTRL) & ~(SHA_CTRL_AREA_BLOCK)) | num_blocks);

	// fire up hashing and wait till its finished
	write32(SHA_CTRL, read32(SHA_CTRL) | SHA_CTRL_FLAG_EXEC);
	while (read32(SHA_CTRL) & SHA_CTRL_FLAG_EXEC);

	// show results
	printf("We proudly present the hash value of the string \"%s\":\n", block);
	sha1_showresult();
}
