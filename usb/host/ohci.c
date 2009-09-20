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
	td->nexttd = ACCESS_LE(virt_to_phys(td));
	//td->nexttd = ACCESS_LE(0);
	if(bsize == 0) {
		td->cbp = td->be = ACCESS_LE(0);
	} else {
		//td->cbp = ACCESS_LE(virt_to_phys(memalign(16, bsize))); //memailgn required here?
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



/**
 * Enqueue a transfer descriptor.
 */
u8 hcdi_enqueue(usb_transfer_descriptor *td) {
	control_quirk(); //required? YES! :O
	u32 tmptdbuffer;

	static struct endpoint_descriptor dummyconfig;
	memset(&dummyconfig, 0, 16);
	dummyconfig.flags = ACCESS_LE(OHCI_ENDPOINT_GENERAL_FORMAT);
	dummyconfig.headp = dummyconfig.tailp = dummyconfig.nexted = ACCESS_LE(0);

	printf(	"===========================\n"
			"===========================\n");
	printf("td->buffer(1): 0x%08X\n", (void*)td->buffer);
	sync_before_read(&hcca_oh0, 256);
	printf("done head (nach sync): 0x%08X\n", ACCESS_LE(hcca_oh0.done_head));
	printf("HCCA->frame_no: %d\nhcca->hccapad1: %d\n",
			((ACCESS_LE(hcca_oh0.frame_no) & 0xffff)>>16),
			ACCESS_LE(hcca_oh0.frame_no)&0x0000ffff );

	struct general_td *tmptd = allocate_general_td(td->actlen);
	(void) memcpy((void*) (phys_to_virt(ACCESS_LE(tmptd->cbp))), td->buffer, td->actlen); 

	tmptd->flags &= ACCESS_LE(~OHCI_TD_DIRECTION_PID_MASK);
	switch(td->pid) {
		case USB_PID_SETUP:
			printf("pid_setup\n");
			tmptd->flags |= ACCESS_LE(OHCI_TD_DIRECTION_PID_SETUP);
			break;
		case USB_PID_OUT:
			printf("pid_out\n");
			tmptd->flags |= ACCESS_LE(OHCI_TD_DIRECTION_PID_OUT);
			break;
		case USB_PID_IN:
			printf("pid_in\n");
			tmptd->flags |= ACCESS_LE(OHCI_TD_DIRECTION_PID_IN);
			break;
	}
	tmptd->flags |= ACCESS_LE((td->togl) ? OHCI_TD_TOGGLE_1 : OHCI_TD_TOGGLE_0);
	//tmptd->flags |= ACCESS_LE(OHCI_TD_TOGGLE_1);

	printf("tmptd hexdump (before) 0x%08X:\n", tmptd);
	hexdump(tmptd, sizeof(struct general_td));
	//save buffer adress here; HC may change tmptd->cbp
	tmptdbuffer = (u32) phys_to_virt(ACCESS_LE(tmptd->cbp)); 
	printf("tmptd->cbp hexdump (before) 0x%08X:\n", phys_to_virt(ACCESS_LE(tmptd->cbp)));
	hexdump((void*) phys_to_virt(ACCESS_LE(tmptd->cbp)), td->actlen);

	sync_after_write(tmptd, sizeof(struct general_td));
	sync_after_write((void*) phys_to_virt(ACCESS_LE(tmptd->cbp)), td->actlen);

#define ED_MASK ((u32)~0x0f) 
	dummyconfig.headp = ACCESS_LE(virt_to_phys((void*) ((u32)tmptd & ED_MASK)));

	dummyconfig.flags |= ACCESS_LE(OHCI_ENDPOINT_LOW_SPEED | 
		OHCI_ENDPOINT_SET_DEVICE_ADDRESS(td->devaddress) | 
		OHCI_ENDPOINT_SET_ENDPOINT_NUMBER(td->endpoint) |
		OHCI_ENDPOINT_SET_MAX_PACKET_SIZE(td->maxp));

	printf("dummyconfig hexdump (before) 0x%08X:\n", &dummyconfig);
	hexdump((void*) &dummyconfig, 16);

	sync_after_write(&dummyconfig, 16);
	write32(OHCI0_HC_CTRL_HEAD_ED, virt_to_phys(&dummyconfig));

	printf("OHCI_CTRL_CLE: 0x%08X || ", read32(OHCI0_HC_CONTROL)&OHCI_CTRL_CLE);
	printf("OHCI_CLF: 0x%08X\n", read32(OHCI0_HC_COMMAND_STATUS)&OHCI_CLF);
	set32(OHCI0_HC_CONTROL, OHCI_CTRL_CLE);
	write32(OHCI0_HC_COMMAND_STATUS, OHCI_CLF);

	//printf("+++++++++++++++++++++++++++++\n");
	/* spin until the controller is done with the control list */
	//printf("current: 0x%08X\n", current);

	//don't use this quirk stuff here!
#if 1
	while(!read32(OHCI0_HC_CTRL_CURRENT_ED)) {
	}
#endif

	udelay(20000);
	u32 current = read32(OHCI0_HC_CTRL_CURRENT_ED);
	printf("current: 0x%08X\n", current);
	printf("+++++++++++++++++++++++++++++\n");
	udelay(20000);

	sync_before_read(tmptd, sizeof(struct general_td));
	printf("tmptd hexdump (after) 0x%08X:\n", tmptd);
	hexdump(tmptd, sizeof(struct general_td));
	dbg_td_flag(ACCESS_LE(tmptd->flags));

	sync_before_read((void*) phys_to_virt(ACCESS_LE(tmptd->cbp)), td->actlen);
	printf("tmptd->cbp hexdump (after) 0x%08X:\n", phys_to_virt(ACCESS_LE(tmptd->cbp)));
	hexdump((void*) phys_to_virt(ACCESS_LE(tmptd->cbp)), td->actlen);

	sync_before_read(&dummyconfig, 16);
	printf("dummyconfig hexdump (after) 0x%08X:\n", &dummyconfig);
	hexdump((void*) &dummyconfig, 16);

	sync_before_read(&hcca_oh0, 256);
	printf("done head (nach sync): 0x%08X\n", ACCESS_LE(hcca_oh0.done_head));

	printf("td->buffer(1): 0x%08X\n", (void*)td->buffer);
	struct general_td* donetd = phys_to_virt(ACCESS_LE(hcca_oh0.done_head)&~1);
	sync_before_read(donetd, 16);
	printf("done head hexdump: 0x%08X\n", donetd);
	hexdump((void*) donetd, 16);
	printf("td->buffer(2): 0x%08X\n", (void*)td->buffer);

	sync_before_read((void*) phys_to_virt(ACCESS_LE(tmptd->cbp)), td->actlen);
	printf("td->buffer: 0x%08X\np2v(A_L(tmptd->cbp: 0x%08X\ntd->actlen: %d\n", (void*) (td->buffer), phys_to_virt(ACCESS_LE(tmptd->cbp)), td->actlen);
	(void) memcpy((void*) (td->buffer), phys_to_virt(ACCESS_LE(tmptd->cbp)), td->actlen);

	write32(OHCI0_HC_CONTROL, read32(OHCI0_HC_CONTROL)&~OHCI_CTRL_CLE);
	dummyconfig.headp = dummyconfig.tailp = dummyconfig.nexted = ACCESS_LE(0);


	/* 
	 * TD should be free'd after taking it from the done queue.
	 * but we are very very dirty and do it anyway :p
	 */

	/* only when a buffer is allocated */
	if(td->actlen)
		free((void*)tmptdbuffer);
	free(tmptd);
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

	//configure_ports((u8)1);
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

		wait_ms(150);

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
		//configure_ports(0);
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
	/*
	printf("***** &hcca: %08X, HcHCCA: %08X\n", 
			&hcca_oh0,
			read32(OHCI0_HC_HCCA));
	*/
	printf("***** HcControlCurrentED: %08X\n", read32(OHCI0_HC_CTRL_CURRENT_ED));
	printf("***** hcca.frame_no: %d, HcFmNumber: %d *****\n",
		ACCESS_LE(hcca_oh0.frame_no),
		read32(OHCI0_HC_FM_NUMBER));
}

static void show_port_status(u32 portreg)
{
	u32 status = read32(portreg);
	printf("port status register summary:\n-----------------------------\n");
	printf("CurrentConnectStatus:      %d\n", (status>>0)&1);
	printf("PortEnableStatus:          %d\n", (status>>1)&1);
	printf("PortSuspendStatus:         %d\n", (status>>2)&1);
	printf("PowerOverCurrentIndicator: %d\n", (status>>3)&1);
	printf("PortResetStatus:           %d\n", (status>>4)&1);
	printf("PortPowerStatus:           %d\n", (status>>8)&1);
	printf("LowSpeedDeviceAttached:    %d\n", (status>>9)&1);
	printf("ConnectStatusChange:       %d\n", (status>>16)&1);
	printf("PortEnableStatusChange:    %d\n", (status>>17)&1);
	printf("PortSuspendStatusChange:   %d\n", (status>>18)&1);
	printf("OvercurrentIndicatorChange:%d\n", (status>>19)&1);
	printf("PortResetStatusChange:     %d\n", (status>>20)&1);
}

void get_device_descriptor()
{
	int i;
	u8 new_attached_device = 0;
	u32 reg;

	/* enable and reset ports for new device */
	for (reg=OHCI0_HC_RH_PORT_STATUS_1; reg<=OHCI0_HC_RH_PORT_STATUS_2; reg+=4) {
		u32 status = read32(reg);

		/* check for new attached device */
		if ((status & RH_PS_CSC) && (status & RH_PS_CCS)) {
			printf("--- new device on port %d!\n", (reg-OHCI0_HC_RH_PORT_STATUS_1)/4+1);

			/* ack connect status change */
			write32(reg, RH_PS_CSC);

			/* enable port */
			write32(reg, RH_PS_PES);
			if (!(read32(reg) & RH_PS_PES)) {
				printf("--- couldn't activate port -> fail!\n");
				return;
			}
			printf("--- port enabled\n");

			/* reset port */
			write32(reg, RH_PS_PRS);
			while(!(read32(reg) & RH_PS_PRSC));

			/* ack reset status change */
			write32(reg, RH_PS_PRSC);
			printf("--- port reset successful\n");

			new_attached_device = 1;
			break;
		}
	}

	/* only continue if there was a device */
	if (!new_attached_device) {
		printf("--- no new device found, returning\n");
		return;
	}

	/* try to send a GetDescriptor control message - build setup data0 package*/
	u8 buffer[64];
	memset(buffer, 0xcc, 64);
	buffer[0] = 0x80;
	buffer[1] = 0x06;
	buffer[2] = 0x00;
	buffer[3] = 0x01;
	buffer[4] = 0x00;
	buffer[5] = 0x00;
	buffer[6] = 0x12;
	buffer[7] = 0x00;

	/* build single transfer descriptor */
	struct general_td *td = memalign(16, sizeof(struct general_td));
	td->flags = ACCESS_LE(7<<21); /* only set DelayInterrupt to 111, all other fields to zero */
	td->cbp = ACCESS_LE(virt_to_phys(&(buffer[0])));
	td->be = ACCESS_LE(virt_to_phys(&(buffer[7])));
	td->nexttd = ACCESS_LE(0);
	printf("--- td built on address %08X\n", (void*)td);

	/* build single endpoint descriptor */
	struct endpoint_descriptor *ed = memalign(16, sizeof(struct endpoint_descriptor));
	/* FunctionAddress=0, EndpointAddress=0, Direction=0, Skip=0, Format=0, Speed=1, MPS=8 */
	ed->flags = ACCESS_LE((8<<16) | (1<<13));
	ed->headp = ACCESS_LE(virt_to_phys(td));
	ed->tailp = ACCESS_LE(0);
	ed->nexted = ACCESS_LE(0);
	printf("--- ed built on address %08X\n", (void*)ed);

	/* flush all that stuff and tell controller to start working! */
	sync_after_write(td, sizeof(struct general_td));
	sync_after_write(ed, sizeof(struct endpoint_descriptor));
	sync_after_write(buffer, 64);
	/*
	printf("\n=============== td hexdump ===============\n");
	hexdump(td, sizeof(struct general_td));

	printf("\n=============== ed hexdump ===============\n");
	hexdump(ed, sizeof(struct endpoint_descriptor));
	*/

	//control_quirk();
	write32(OHCI0_HC_CTRL_HEAD_ED, virt_to_phys(ed));
	//write32(OHCI0_HC_CTRL_HEAD_ED, (u32)ed);
	write32(OHCI0_HC_COMMAND_STATUS, OHCI_CLF);
	set32(OHCI0_HC_CONTROL, OHCI_CTRL_CLE);
	printf("--- told ohci controller to start working\n");

	u32 current_ed, counter=10;
	while((current_ed = read32(OHCI0_HC_CTRL_CURRENT_ED))) {
		printf("current_ed: %08X\n", current_ed);
		printf("\tHcCommandStatusReg: %08X\n", read32(OHCI0_HC_COMMAND_STATUS));

		counter--;
		if (counter == 0)
			break;
	}
	printf("--- finished ;-)\n");
	sync_before_read(buffer, 64);
	hexdump(buffer, 64);

	free(td);
	free(ed);
}
