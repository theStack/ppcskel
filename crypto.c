#include "hollywood.h"
#include "crypto.h"
#include "bootmii_ppc.h"
#include "string.h"
#include "malloc.h"

void sha1_reset(void)
{
	// reset sha-1 engine
	write32(SHA_CMD, read32(SHA_CMD) & ~(SHA_CMD_FLAG_EXEC));
	while ((read32(SHA_CMD) & SHA_CMD_FLAG_EXEC) != 0);

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
	u8 *block = memalign(64, 64*num_blocks);
	memcpy(block, src, 64*num_blocks);

	// royal flush :)
	sync_after_write(block, 64*num_blocks);

	// tell sha1 controller the block source address
	write32(SHA_SRC, virt_to_phys(block));
	
	// tell sha1 controller number of blocks
	if (num_blocks != 0)
		num_blocks--;
	write32(SHA_CMD, (read32(SHA_CMD) & ~(SHA_CMD_AREA_BLOCK)) | num_blocks);

	// fire up hashing and wait till its finished
	write32(SHA_CMD, read32(SHA_CMD) | SHA_CMD_FLAG_EXEC);
	while (read32(SHA_CMD) & SHA_CMD_FLAG_EXEC);

	// free the aligned data
	free(block);

	// show results
	printf("We proudly present the hash value of the string \"%s\":\n", block);
	sha1_showresult();
}
