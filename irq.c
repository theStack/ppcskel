/*
	ppcskel - a Free Software replacement for the Nintendo/BroadOn IOS.
	IRQ support

Copyright (C) 2008, 2009	Hector Martin "marcan" <marcan@marcansoft.com>
Copyright (C) 2008, 2009	Sven Peter <svenpeter@gmail.com>
Copyright (C) 2009			Andre Heider "dhewg" <dhewg@wiibrew.org>

# This code is licensed to you under the terms of the GNU GPL, version 2;
# see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

#include "irq.h"
#include "hollywood.h"
#include "ipc.h"
#include "bootmii_ppc.h"
//debug only
#include "printf.h"

void irq_initialize(void)
{
	// clear flipper-pic (processor interface)
	write32(BW_PI_IRQMASK, 0);
	write32(BW_PI_IRQFLAG, 0xffffffff);

	// clear hollywood-pic
	write32(HW_PPCIRQMASK, 0);
	write32(HW_PPCIRQFLAG, 0xffffffff);

	printf("PPCIRQMASK: 0x%08X\n", read32(HW_PPCIRQMASK));


	//??? -- needed?!
	//write32(HW_ARMIRQFLAG, 0); // this does nothing?
	write32(HW_PPCIRQMASK+0x20, 0);

	_CPU_ISR_Enable()
}

void irq_shutdown(void)
{
	write32(HW_PPCIRQMASK, 0);
	write32(HW_PPCIRQFLAG, 0xffffffff);
	(void) irq_kill();
}

void irq_handler(void)
{
	u32 enabled = read32(BW_PI_IRQMASK);
	u32 flags = read32(BW_PI_IRQFLAG);
	printf("flags1: 0x%08X\n", flags);

	flags = flags & enabled;

	if (flags & (1<<BW_PI_IRQ_RESET)) { 
		write32(BW_PI_IRQFLAG, 1<<BW_PI_IRQ_RESET);
		printf("IRQ-BW RESET\n");
	}
	if (flags & (1<<BW_PI_IRQ_HW)) { //HW-PIC IRQ
		u32 hw_enabled = read32(HW_PPCIRQMASK);
		u32 hw_flags = read32(HW_PPCIRQFLAG);

		printf("In IRQ handler: 0x%08x 0x%08x 0x%08x\n", hw_enabled, hw_flags, hw_flags & hw_enabled);

		hw_flags = hw_flags & hw_enabled;

		if(hw_flags & IRQF_TIMER) {
			printf("ppcirqmask: %08X\n", read32(HW_PPCIRQMASK));
			printf("[before_flag_clear] ppcirqflag: %08X\n", read32(HW_PPCIRQFLAG));
			printf("[before_flag_clear] pi irqflag: %08X\n", read32(BW_PI_IRQFLAG));
			write32(HW_PPCIRQFLAG, IRQF_TIMER);
			printf("[after_flag_clear] ppcirqflag: %08X\n", read32(HW_PPCIRQFLAG));
			printf("[after_flag_clear] pi irqflag: %08X\n", read32(BW_PI_IRQFLAG));

			// save timer value and wait till that fuckin' irq occures again
			u32 starttime = read32(HW_TIMER);
			while(!(read32(BW_PI_IRQFLAG) & (1<<BW_PI_IRQ_HW))); 

			// boom!
			u32 lasted = read32(HW_TIMER) - starttime;
			printf("it took the hardware %d timer counts till that pi irq occured again (that's ~%d us)...\n",
				lasted, (lasted*527)/1000);
			printf("[after_irq] ppcirqflag: %08X\n", read32(HW_PPCIRQFLAG));
			printf("[after_irq] pi irqflag: %08X\n", read32(BW_PI_IRQFLAG));
			while(1){}

		}
		if(hw_flags & IRQF_NAND) {
			//		printf("IRQ: NAND\n");
			// hmmm... should be done by mini?
			write32(NAND_CMD, 0x7fffffff); // shut it up
			write32(HW_PPCIRQFLAG, IRQF_NAND);
			//nand_irq();
		}
		if(hw_flags & IRQF_GPIO1B) {
			//		printf("IRQ: GPIO1B\n");
			// hmmm... should be done by mini?
			write32(HW_GPIO1BINTFLAG, 0xFFFFFF); // shut it up
			write32(HW_PPCIRQFLAG, IRQF_GPIO1B);
		}
		if(hw_flags & IRQF_GPIO1) {
			//		printf("IRQ: GPIO1\n");
			// hmmm... should be done by mini?
			write32(HW_GPIO1INTFLAG, 0xFFFFFF); // shut it up
			write32(HW_PPCIRQFLAG, IRQF_GPIO1);
		}
		if(hw_flags & IRQF_RESET) {
			//		printf("IRQ: RESET\n");
			write32(HW_PPCIRQFLAG, IRQF_RESET);
		}
		if(hw_flags & IRQF_IPC) {
			//printf("IRQ: IPC\n");
			//not necessary here?
			//ipc_irq();
			write32(HW_PPCIRQFLAG, IRQF_IPC);
		}
		if(hw_flags & IRQF_AES) {
			//		printf("IRQ: AES\n");
			write32(HW_PPCIRQFLAG, IRQF_AES);
		}
		if (hw_flags & IRQF_SDHC) {
			//		printf("IRQ: SDHC\n");
			write32(HW_PPCIRQFLAG, IRQF_SDHC);
			//sdhc_irq();
		}
		if (hw_flags & IRQF_OHCI0) {
			printf("IRQ: OHCI0\n");
			printf("ppcirqmask: %08X\n", read32(HW_PPCIRQMASK));
			printf("[before_flag_clear] ppcirqflag: %08X\n", read32(HW_PPCIRQFLAG));
			printf("[before_flag_clear] pi irqflag: %08X\n", read32(BW_PI_IRQFLAG));
			write32(HW_PPCIRQFLAG, IRQF_OHCI0);
			printf("[after_flag_clear] ppcirqflag: %08X\n", read32(HW_PPCIRQFLAG));
			printf("[after_flag_clear] pi irqflag: %08X\n", read32(BW_PI_IRQFLAG));
			while(1){}
			//TODO: ohci0_irq();
		}
		if (hw_flags & IRQF_OHCI1) {
			printf("IRQ: OHCI1\n");
			write32(HW_PPCIRQFLAG, IRQF_OHCI1);
			//TODO: ohci1_irq();
		}

		hw_flags &= ~IRQF_ALL;
		if(hw_flags) {
			printf("IRQ: unknown 0x%08x\n", hw_flags);
			write32(HW_PPCIRQFLAG, hw_flags);
		}

		printf("hw_flags1: 0x%08x\n", read32(HW_PPCIRQFLAG));

		// quirk for flipper pic? TODO :/
		//write32(HW_PPCIRQMASK, 0);

		//write32(BW_PI_IRQFLAG, 1<<BW_PI_IRQ_HW);
		printf("flags2: 0x%08X\n", read32(BW_PI_IRQFLAG));
		
		//write32(HW_PPCIRQMASK, hw_enabled);
	}
}

void irq_bw_enable(u32 irq)
{
	set32(BW_PI_IRQMASK, 1<<irq);
}

void irq_bw_disable(u32 irq) {
	clear32(BW_PI_IRQMASK, 1<<irq);
}

void irq_hw_enable(u32 irq)
{
	set32(HW_PPCIRQMASK, 1<<irq);
}

void irq_hw_disable(u32 irq)
{
	clear32(HW_PPCIRQMASK, 1<<irq);
}

u32 irq_kill() {
	u32 cookie;
	_CPU_ISR_Disable(cookie);
	return cookie;
}

void irq_restore(u32 cookie) {
	_CPU_ISR_Restore(cookie);
}
