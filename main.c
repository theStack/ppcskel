/*
        BootMii - a Free Software replacement for the Nintendo/BroadOn bootloader.
        Requires mini.

Copyright (C) 2008, 2009        Haxx Enterprises <bushing@gmail.com>
Copyright (C) 2009              Andre Heider "dhewg" <dhewg@wiibrew.org>
Copyright (C) 2008, 2009        Hector Martin "marcan" <marcan@marcansoft.com>
Copyright (C) 2008, 2009        Sven Peter <svenpeter@gmail.com>
Copyright (C) 2009              John Kelley <wiidev@kelley.ca>

# This code is licensed to you under the terms of the GNU GPL, version 2;
# see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

#include "bootmii_ppc.h"
#include "string.h"
#include "ipc.h"
#include "mini_ipc.h"
#include "nandfs.h"
#include "fat.h"
#include "malloc.h"
#include "diskio.h"
#include "printf.h"
#include "video_low.h"
#include "input.h"
#include "console.h"
#include "hollywood.h"
#include "crypto.h"

#define MINIMUM_MINI_VERSION 0x00010001

otp_t otp;
seeprom_t seeprom;

static void dsp_reset(void)
{
	write16(0x0c00500a, read16(0x0c00500a) & ~0x01f8);
	write16(0x0c00500a, read16(0x0c00500a) | 0x0010);
	write16(0x0c005036, 0);
}

static char ascii(char s) {
  if(s < 0x20) return '.';
  if(s > 0x7E) return '.';
  return s;
}

void hexdump(void *d, int len) {
  u8 *data;
  int i, off;
  data = (u8*)d;
  for (off=0; off<len; off += 16) {
    printf("%08x  ",off);
    for(i=0; i<16; i++)
      if((i+off)>=len) printf("   ");
      else printf("%02x ",data[off+i]);

    printf(" ");
    for(i=0; i<16; i++)
      if((i+off)>=len) printf(" ");
      else printf("%c",ascii(data[off+i]));
    printf("\n");
  }
}
	
void testOTP(void)
{
	printf("reading OTP...\n");
	getotp(&otp);
	printf("read OTP!\n");
	printf("OTP:\n");
	hexdump(&otp, sizeof(otp));

	printf("reading SEEPROM...\n");
	getseeprom(&seeprom);
	printf("read SEEPROM!\n");
	printf("SEEPROM:\n");
	hexdump(&seeprom, sizeof(seeprom));
}

int main(void)
{
	int vmode = -1;
	exception_init();
	dsp_reset();

	/* write ahbprot */
	write32(HW_AHBPROT, 0xffffffff);

	// clear interrupt mask
	write32(0x0c003004, 0);

	ipc_initialize();
	ipc_slowping();

	gecko_init();
	input_init();
	init_fb(vmode);

	VIDEO_Init(vmode);
	VIDEO_SetFrameBuffer(get_xfb());
	VISetupEncoder();

	u32 version = ipc_getvers();
	u16 mini_version_major = version >> 16 & 0xFFFF;
	u16 mini_version_minor = version & 0xFFFF;
	printf("Mini version: %d.%0d\n", mini_version_major, mini_version_minor);

	if (version < MINIMUM_MINI_VERSION) {
		printf("Sorry, this version of MINI (armboot.bin)\n"
				"is too old, please update to at least %d.%0d.\n", 
				(MINIMUM_MINI_VERSION >> 16), (MINIMUM_MINI_VERSION & 0xFFFF));
		for (;;) 
			; // better ideas welcome!
	}

	print_str_noscroll(112, 112, "ohai, world!\n");

	//testOTP();

	char buf[1024];
	u8 buffer2[64];
	u32 *ret;
	u8 brut0;
	u8 brut1;
	u8 brut2;
	u8 brut3;
	u32 round=0;
	//string to hash
	//static u8 buffer[64] ALIGNED(64) = {'1', '3', '3', '7'};
	static u8 buffer[64] ALIGNED(64) = "muuh";

	for(brut3='0'-1; brut3<('9'+1); brut3++) {
		for(brut2='0'-1; brut2<('9'+1); brut2++) {
			for(brut1='0'-1; brut1<('9'+1); brut1++) {
				for(brut0='0'; brut0<('9'+1); brut0++) {
					u8 ca=0;
					sha_reset();
					round++;

					//padding (must done in software)
					buffer[4] = brut0;

					if(brut1 == '0'-1)
						ca++;
					else
						buffer[5] = brut1;

					if(brut2 == '0'-1)
						ca++;
					else
						buffer[6] = brut2;

					if(brut3 == '0'-1)
						ca++;
					else
						buffer[7] = brut3;

					buffer[8-ca] = 0x80; //add bit 1
					u8 itmp;
					for(itmp=9-ca; itmp <63; itmp++) {
						buffer[itmp]=0;
					}
					buffer[63] = 64-(ca*8); //last 64bit contain the length (bit_length("asdfXXXX") = 8*8 = 64)

					ret = sha_decrypt(buffer,1);
					int i=120;
					int j=0;

					strlcpy(buffer2, buffer, 9-ca);

					sprintf(buf, "round %i with value \"%s\"\n", round, buffer2);
					print_str_noscroll(112, 100, buf);
					for(;j<5;j++) {
						sprintf(buf, "sha_h%i: %08x\n", j, ret[j]);
						print_str_noscroll(112, i, buf);
						printf(buf);
						i+=20;
					}
					udelay(100000);
					if(!(0xff000000&ret[0]))
						goto exit;

				}
			}
		}
	}
exit:

	printf("bye, world!\n");

	return 0;
}

