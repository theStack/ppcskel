/*
       ppcskel - a Free Software replacement for the Nintendo/BroadOn bootloader.
       ohci hardware support

Copyright (C) 2009     Bernhard Urban <lewurm@gmx.net>
Copyright (C) 2009     Sebastian Falbesoner <sebastian.falbesoner@gmail.com>

# This code is licensed to you under the terms of the GNU GPL, version 2;
# see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

#include "../../bootmii_ppc.h"
#include "../../hollywood.h"
#include "../../irq.h"
#include "../../string.h"
#include "../../malloc.h"
#include "ohci.h"
#include "host.h"
#include "../usbspec/usb11spec.h"

// macro for accessing u32 variables that need to be in little endian byte order;
// whenever you read or write from an u32 field that the ohci host controller
// will read or write from too, use this macro for access!
#define ACCESS_LE(dword) (u32)( (((dword) & 0xFF000000) >> 24) | \
			   (((dword) & 0x00FF0000) >> 8)  | \
			   (((dword) & 0x0000FF00) << 8)  | \
			   (((dword) & 0x000000FF) << 24) )

static struct endpoint_descriptor *allocate_endpoint();
static struct general_td *allocate_general_td(size_t);
static void control_quirk();
static void dbg_op_state();
static void dbg_td_flag(u32 flag);
static void configure_ports(u8 from_init);
static void setup_port(u32 reg, u8 from_init);

static struct ohci_hcca hcca_oh0;


static struct endpoint_descriptor *allocate_endpoint()
{
	struct endpoint_descriptor *ep;
	ep = (struct endpoint_descriptor *)memalign(16, sizeof(struct endpoint_descriptor));
	ep->flags = ACCESS_LE(OHCI_ENDPOINT_GENERAL_FORMAT);
	ep->headp = ep->tailp = ep->nexted = ACCESS_LE(0);
	return ep;
}

static struct general_td *allocate_general_td(size_t bsize)
{
	struct general_td *td;
	td = (struct general_td *)memalign(16, sizeof(struct general_td));
	td->flags = ACCESS_LE(0);
	// TODO !! nexttd?
	//td->nexttd = ACCESS_LE(virt_to_phys(td));
	td->nexttd = ACCESS_LE(0);
	if(bsize == 0) {
		td->cbp = td->be = ACCESS_LE(0);
	} else {
		//align it to 4kb? :O
		//td->cbp = ACCESS_LE(virt_to_phys(memalign(4096, bsize))); //memailgn required here?
		td->cbp = ACCESS_LE(virt_to_phys(malloc(bsize)));
		memset(phys_to_virt(ACCESS_LE(td->cbp)), 0, bsize);
		td->be = ACCESS_LE(ACCESS_LE(td->cbp) + bsize - 1);
	}
	return td;
}

static void control_quirk()
{
	static struct endpoint_descriptor *ed = 0; /* empty ED */
	static struct general_td *td = 0; /* dummy TD */
	u32 head;
	u32 current;
	u32 status;

	/*
	 * One time only.
	 * Allocate and keep a special empty ED with just a dummy TD.
	 */
	if (!ed) {
		ed = allocate_endpoint();
		if (!ed)
			return;

		td = allocate_general_td(0);
		if (!td) {
			free(ed);
			ed = NULL;
			return;
		}

#define ED_MASK ((u32)~0x0f)
		ed->tailp = ed->headp = ACCESS_LE(virt_to_phys((void*) ((u32)td & ED_MASK)));
		ed->flags |= ACCESS_LE(OHCI_ENDPOINT_DIRECTION_OUT);
	}

	/*
	 * The OHCI USB host controllers on the Nintendo Wii
	 * video game console stop working when new TDs are
	 * added to a scheduled control ED after a transfer has
	 * has taken place on it.
	 *
	 * Before scheduling any new control TD, we make the
	 * controller happy by always loading a special control ED
	 * with a single dummy TD and letting the controller attempt
	 * the transfer.
	 * The controller won't do anything with it, as the special
	 * ED has no TDs, but it will keep the controller from failing
	 * on the next transfer.
	 */
	head = read32(OHCI0_HC_CTRL_HEAD_ED);
	if (head) {
		printf("head: 0x%08X\n", head);
		/*
		 * Load the special empty ED and tell the controller to
		 * process the control list.
		 */
		sync_after_write(ed, 16);
		sync_after_write(td, 16);
		write32(OHCI0_HC_CTRL_HEAD_ED, virt_to_phys(ed));

		status = read32(OHCI0_HC_CONTROL);
		set32(OHCI0_HC_CONTROL, OHCI_CTRL_CLE);
		write32(OHCI0_HC_COMMAND_STATUS, OHCI_CLF);

		/* spin until the controller is done with the control list */
		current = read32(OHCI0_HC_CTRL_CURRENT_ED);
		while(!current) {
			udelay(10);
			current = read32(OHCI0_HC_CTRL_CURRENT_ED);
		}

		printf("current: 0x%08X\n", current);
			
		/* restore the old control head and control settings */
		write32(OHCI0_HC_CONTROL, status);
		write32(OHCI0_HC_CTRL_HEAD_ED, head);
	} else {
		printf("nohead!\n");
	}
}


static void dbg_op_state() 
{
	switch (read32(OHCI0_HC_CONTROL) & OHCI_CTRL_HCFS) {
		case OHCI_USB_SUSPEND:
			printf("ohci-- OHCI_USB_SUSPEND\n");
			break;
		case OHCI_USB_RESET:
			printf("ohci-- OHCI_USB_RESET\n");
			break;
		case OHCI_USB_OPER:
			printf("ohci-- OHCI_USB_OPER\n");
			break;
		case OHCI_USB_RESUME:
			printf("ohci-- OHCI_USB_RESUME\n");
			break;
	}
}

static void dbg_td_flag(u32 flag)
{
	printf("**************** dbg_td_flag: 0x%08X ***************\n", flag);
	printf("CC: %X\tshould be 0, see page 32 (ohci spec)\n", (flag>>28)&0xf);
	printf("EC: %X\tsee page 20 (ohci spec)\n", (flag>>26)&3);
	printf(" T: %X\n", (flag>>24)&3);
	printf("DI: %X\n", (flag>>21)&7);
	printf("DP: %X\n", (flag>>19)&3);
	printf(" R: %X\n", (flag>>18)&1);
	printf("********************************************************\n");
}

static void general_td_fill(struct general_td *dest, usb_transfer_descriptor *src)
{
	(void) memcpy((void*) (phys_to_virt(ACCESS_LE(dest->cbp))), src->buffer, src->actlen); 
	dest->flags &= ACCESS_LE(~OHCI_TD_DIRECTION_PID_MASK);
	switch(src->pid) {
		case USB_PID_SETUP:
			printf("pid_setup\n");
			dest->flags |= ACCESS_LE(OHCI_TD_DIRECTION_PID_SETUP);
			dest->flags |= ACCESS_LE(OHCI_TD_TOGGLE_0);
			dest->flags |= ACCESS_LE(OHCI_TD_BUFFER_ROUNDING);
			break;
		case USB_PID_OUT:
			printf("pid_out\n");
			dest->flags |= ACCESS_LE(OHCI_TD_DIRECTION_PID_OUT);
			dest->flags |= ACCESS_LE(OHCI_TD_BUFFER_ROUNDING);

			/*
			 * TODO: just temporary solution!
			 * there can be also regular PID_OUT pakets
			 */
			dest->flags |= ACCESS_LE(OHCI_TD_TOGGLE_1);
			break;
		case USB_PID_IN:
			printf("pid_in\n");
			dest->flags |= ACCESS_LE(OHCI_TD_DIRECTION_PID_IN);
			dest->flags |= ACCESS_LE(OHCI_TD_BUFFER_ROUNDING);
			/*
			 * let the endpoint do the togglestuff!
			 * TODO: just temporary solution!
			 * there can be also inregular PID_IN pakets (@Status Stage)
			 */
			dest->flags |= ACCESS_LE(OHCI_TD_TOGGLE_CARRY);
#if 0
			/* should be done by HC!
			 * first pid_in start with DATA0 */
			 */
			dummyconfig.headp = ACCESS_LE( src->togl ?
					ACCESS_LE(dummyconfig.headp) | OHCI_ENDPOINT_TOGGLE_CARRY :
					ACCESS_LE(dummyconfig.headp) & ~OHCI_ENDPOINT_TOGGLE_CARRY);
#endif
			break;
	}
	dest->flags |= ACCESS_LE(OHCI_TD_SET_DELAY_INTERRUPT(7));
	sync_after_write(dest, sizeof(struct general_td));
	sync_after_write((void*) phys_to_virt(ACCESS_LE(dest->cbp)), src->actlen);
}

static void dump_address(void *addr, u32 size, const char* str)
{
	sync_before_read(addr, size);
	printf("%s hexdump @ 0x%08X:\n", str, addr);
	hexdump(addr, size);
}

/**
 * Enqueue a transfer descriptor.
 */
u8 first = 0;
u8 hcdi_enqueue(usb_transfer_descriptor *td) {
	static struct general_td *tSetup,*tData;
	static u32 tSetupbuffer, tDatabuffer, tStatusbuffer;
	static u32 tSetupblen, tDatablen, tStatusblen;

	if(first == 0) {
		printf("step 0\n");
		tSetup = allocate_general_td(td->actlen);
		general_td_fill(tSetup, td);
		tSetupbuffer = (u32) phys_to_virt(ACCESS_LE(tSetup->cbp)); 
		tSetupblen = td->actlen;
		first++;
		return 0;
	}
	if(first == 1) {
		printf("step 1\n");
		tData = allocate_general_td(td->actlen);
		general_td_fill(tData, td);
		tDatabuffer = (u32) phys_to_virt(ACCESS_LE(tData->cbp)); 
		tDatablen = td->actlen;
		first++;
		return 0;
	}
	first = 0;
	struct general_td *tStatus = allocate_general_td(td->actlen);
	general_td_fill(tStatus, td);
	tStatusbuffer = (u32) phys_to_virt(ACCESS_LE(tStatus->cbp)); 
	tStatusblen = td->actlen;

	printf(	"===========================\n"
			"===========================\n");
	control_quirk(); //required? YES! :O ... erm... or no? :/ ... in fact I have no idea

	struct endpoint_descriptor *dummyconfig = allocate_endpoint();
	dummyconfig->flags = ACCESS_LE(OHCI_ENDPOINT_GENERAL_FORMAT);
	dummyconfig->headp = dummyconfig->tailp = dummyconfig->nexted = ACCESS_LE(0);
	dummyconfig->flags |= ACCESS_LE(OHCI_ENDPOINT_LOW_SPEED |
			OHCI_ENDPOINT_SET_DEVICE_ADDRESS(td->devaddress) |
			OHCI_ENDPOINT_SET_ENDPOINT_NUMBER(td->endpoint) |
			OHCI_ENDPOINT_SET_MAX_PACKET_SIZE(td->maxp));
	write32(OHCI0_HC_CTRL_HEAD_ED, virt_to_phys(dummyconfig));

#define ED_MASK ((u32)~0x0f) 
	dummyconfig->headp |= ACCESS_LE(virt_to_phys((void*) ((u32)tSetup & ED_MASK)));
	tSetup->nexttd = ACCESS_LE(virt_to_phys((void*) ((u32)tData & ED_MASK)));
	tData->nexttd = ACCESS_LE(virt_to_phys((void*) ((u32)tStatus & ED_MASK)));

	sync_after_write(dummyconfig, 16);
	sync_after_write(tSetup, sizeof(struct general_td));
	sync_after_write(tData, sizeof(struct general_td));
	sync_after_write(tStatus, sizeof(struct general_td));

	dump_address(tSetup, sizeof(struct general_td), "tSetup(before)");
	dump_address((void*) phys_to_virt(ACCESS_LE(tSetup->cbp)), tSetupblen, "tSetup->cbp(before)");

	dump_address(tData, sizeof(struct general_td), "tData(before)");
	dump_address((void*) phys_to_virt(ACCESS_LE(tData->cbp)), tDatablen, "tData->cbp(before)");

	dump_address(tStatus, sizeof(struct general_td), "tStatus(before)");
	dump_address((void*) phys_to_virt(ACCESS_LE(tStatus->cbp)), tStatusblen, "tStatus->cbp(before)");

	dump_address(dummyconfig, sizeof(struct endpoint_descriptor), "dummyconfig(before)");

	printf("ctrl head: 0x%08X\n", read32(OHCI0_HC_CTRL_HEAD_ED));
	/* trigger control list */
	set32(OHCI0_HC_CONTROL, OHCI_CTRL_CLE);
	write32(OHCI0_HC_COMMAND_STATUS, OHCI_CLF);

	//don't use this quirk stuff here!
#if 1
	while(!read32(OHCI0_HC_CTRL_CURRENT_ED)) {
	}
#endif

	udelay(1000000);
	u32 current = read32(OHCI0_HC_CTRL_CURRENT_ED);
	printf("current: 0x%08X\n", current);
	printf("+++++++++++++++++++++++++++++\n");
	udelay(10000000);

	dump_address(tSetup, sizeof(struct general_td), "tSetup(after)");
	dump_address((void*) phys_to_virt(ACCESS_LE(tSetup->cbp)), tSetupblen, "tSetup->cbp(after)");
	dump_address((void*) tSetupbuffer, tSetupblen, "tSetupbuffer");
	dbg_td_flag(ACCESS_LE(tSetup->flags));

	dump_address(tData, sizeof(struct general_td), "tData(after)");
	dump_address((void*) phys_to_virt(ACCESS_LE(tData->cbp)), tDatablen, "tData->cbp(after)");
	dump_address((void*) tDatabuffer, tDatablen, "tDatabuffer");
	dbg_td_flag(ACCESS_LE(tData->flags));

	dump_address(tStatus, sizeof(struct general_td), "tStatus(after)");
	//dump_address((void*) phys_to_virt(ACCESS_LE(tStatus->cbp)), tStatusblen, "tStatus->cbp(after)");
	//dump_address((void*) tStatusbuffer, tStatusblen, "tStatusbuffer");
	dbg_td_flag(ACCESS_LE(tStatus->flags));

	dump_address(dummyconfig, sizeof(struct endpoint_descriptor), "dummyconfig(after)");

	/* disable control list */
	write32(OHCI0_HC_CONTROL, read32(OHCI0_HC_CONTROL)&~OHCI_CTRL_CLE);

	/* 
	 * TD should be free'd after taking it from the done queue.
	 * but we are very very dirty and do it anyway :p
	 */

	/* only when a buffer is allocated */
#if 0
	if(td->actlen)
		free((void*)tStatusbuffer);
	free(tStatus);
#endif
	printf("hcdi_enqueue, done!\n");
	return 0;
}

/**
 * Remove an transfer descriptor from transfer queue.
 */
u8 hcdi_dequeue(usb_transfer_descriptor *td) {
	return 0;
}

void hcdi_init() 
{
	printf("ohci-- init\n");
	dbg_op_state();

	/* disable hc interrupts */
	set32(OHCI0_HC_INT_DISABLE, OHCI_INTR_MIE);

	/* save fmInterval and calculate FSMPS */
#define FSMP(fi) (0x7fff & ((6 * ((fi) - 210)) / 7))
#define FI 0x2edf /* 12000 bits per frame (-1) */
	u32 fmint = read32(OHCI0_HC_FM_INTERVAL) & 0x3fff;
	if(fmint != FI)
		printf("ohci-- fminterval delta: %d\n", fmint - FI);
	fmint |= FSMP (fmint) << 16;

	/* enable interrupts of both usb host controllers */
	set32(EHCI_CTL, EHCI_CTL_OH0INTE | EHCI_CTL_OH1INTE | 0xe0000);

	/* reset HC */
	write32(OHCI0_HC_COMMAND_STATUS, OHCI_HCR);

	/* wait max. 30us */
	u32 ts = 30;
	while ((read32(OHCI0_HC_COMMAND_STATUS) & OHCI_HCR) != 0) {
		 if(--ts == 0) {
			printf("ohci-- FAILED");
			return;
		 }
		 udelay(1);
	}

	/* disable interrupts; 2ms timelimit here! 
	   now we're in the SUSPEND state ... must go OPERATIONAL
	   within 2msec else HC enters RESUME */

	u32 cookie = irq_kill();

	/* Tell the controller where the control and bulk lists are
	 * The lists are empty now. */
	write32(OHCI0_HC_CTRL_HEAD_ED, 0);
	write32(OHCI0_HC_BULK_HEAD_ED, 0);

	/* set hcca adress */
	sync_after_write(&hcca_oh0, 256);
	write32(OHCI0_HC_HCCA, virt_to_phys(&hcca_oh0));

	/* set periodicstart */
#define FIT (1<<31)
	u32 fmInterval = read32(OHCI0_HC_FM_INTERVAL) &0x3fff;
	u32 fit = read32(OHCI0_HC_FM_INTERVAL) & FIT;

	write32(OHCI0_HC_FM_INTERVAL, fmint | (fit ^ FIT));
	write32(OHCI0_HC_PERIODIC_START, ((9*fmInterval)/10)&0x3fff);

	/* testing bla */
	if ((read32(OHCI0_HC_FM_INTERVAL) & 0x3fff0000) == 0 || !read32(OHCI0_HC_PERIODIC_START)) {
		printf("ohci-- w00t, fail!! see ohci-hcd.c:669\n");
	}
	
	/* start HC operations */
	write32(OHCI0_HC_CONTROL, OHCI_CONTROL_INIT | OHCI_USB_OPER);

	/* wake on ConnectStatusChange, matching external hubs */
	write32(OHCI0_HC_RH_STATUS, /*RH_HS_DRWE |*/ RH_HS_LPSC);

	/* Choose the interrupts we care about now, others later on demand */
	write32(OHCI0_HC_INT_STATUS, ~0);
	write32(OHCI0_HC_INT_ENABLE, OHCI_INTR_INIT);

	//wtf?
	wait_ms ((read32(OHCI0_HC_RH_DESCRIPTOR_A) >> 23) & 0x1fe);

	configure_ports((u8)1);
	irq_restore(cookie);

	dbg_op_state();
}

static void configure_ports(u8 from_init)
{
	printf("OHCI0_HC_RH_DESCRIPTOR_A:\t0x%08X\n", read32(OHCI0_HC_RH_DESCRIPTOR_A));
	printf("OHCI0_HC_RH_DESCRIPTOR_B:\t0x%08X\n", read32(OHCI0_HC_RH_DESCRIPTOR_B));
	printf("OHCI0_HC_RH_STATUS:\t\t0x%08X\n", read32(OHCI0_HC_RH_STATUS));
	printf("OHCI0_HC_RH_PORT_STATUS_1:\t0x%08X\n", read32(OHCI0_HC_RH_PORT_STATUS_1));
	printf("OHCI0_HC_RH_PORT_STATUS_2:\t0x%08X\n", read32(OHCI0_HC_RH_PORT_STATUS_2));

	setup_port(OHCI0_HC_RH_PORT_STATUS_1, from_init);
	setup_port(OHCI0_HC_RH_PORT_STATUS_2, from_init);
	printf("configure_ports done\n");
}

static void setup_port(u32 reg, u8 from_init)
{
	u32 port = read32(reg);
	if((port & RH_PS_CCS) && ((port & RH_PS_CSC) || from_init)) {
		write32(reg, RH_PS_CSC);

		wait_ms(120);

		/* clear CSC flag, set PES and start port reset (PRS) */
		write32(reg, RH_PS_PES);
		while(!(read32(reg) & RH_PS_PES)) {
			printf("fu\n");
			return;
		}

		write32(reg, RH_PS_PRS);

		/* spin until port reset is complete */
		while(!(read32(reg) & RH_PS_PRSC)); // hint: it may stuck here
		printf("loop done\n");

		wait_ms(20);

		(void) usb_add_device();
	}
}

void hcdi_irq()
{
	/* read interrupt status */
	u32 flags = read32(OHCI0_HC_INT_STATUS);

	/* when all bits are set to 1 some problem occured */
	if (flags == 0xffffffff) {
		printf("ohci-- Houston, we have a serious problem! :(\n");
		return;
	}

	/* only care about interrupts that are enabled */
	flags &= read32(OHCI0_HC_INT_ENABLE);

	/* nothing to do? */
	if (flags == 0) {
		printf("OHCI Interrupt occured: but not for you! WTF?!\n");
		return;
	}

	printf("OHCI Interrupt occured: ");
	/* UnrecoverableError */
	if (flags & OHCI_INTR_UE) {
		printf("UnrecoverableError\n");
		/* TODO: well, I don't know... nothing,
		 *       because it won't happen anyway? ;-) */
	}

	/* RootHubStatusChange */
	if (flags & OHCI_INTR_RHSC) {
		printf("RootHubStatusChange\n");
		/* TODO: set some next_statechange variable... */
		configure_ports(0);
		write32(OHCI0_HC_INT_STATUS, OHCI_INTR_RD | OHCI_INTR_RHSC);
	}
	/* ResumeDetected */
	else if (flags & OHCI_INTR_RD) {
		printf("ResumeDetected\n");
		write32(OHCI0_HC_INT_STATUS, OHCI_INTR_RD);
		/* TODO: figure out what the linux kernel does here... */
	}

	/* WritebackDoneHead */
	if (flags & OHCI_INTR_WDH) {
		printf("WritebackDoneHead\n");
		/* basically the linux irq handler reverse TDs to their urbs
		 * and set done_head to null.
		 * since we are polling atm, just should do the latter task.
		 * however, this won't work for now (i don't know why...)
		 * TODO!
		 */
#if 0
		sync_before_read(&hcca_oh0, 256);
		hcca_oh0.done_head = 0;
		sync_after_write(&hcca_oh0, 256);
#endif
	}

	/* TODO: handle any pending URB/ED unlinks... */

#define HC_IS_RUNNING() 1 /* dirty, i know... just a temporary solution */
	if (HC_IS_RUNNING()) {
		write32(OHCI0_HC_INT_STATUS, flags);
		write32(OHCI0_HC_INT_ENABLE, OHCI_INTR_MIE);
	}
}

void show_frame_no()
{
	sync_before_read(&hcca_oh0, 256);
	printf("***** frame_no: %d *****\n", ACCESS_LE(hcca_oh0.frame_no));
}
