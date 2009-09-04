/*
       mini - a Free Software replacement for the Nintendo/BroadOn IOS.
       ohci hardware support

Copyright (C) 2009     Bernhard Urban <lewurm@gmx.net>
Copyright (C) 2009     Sebastian Falbesoner <sebastian.falbesoner@gmail.com>

# This code is licensed to you under the terms of the GNU GPL, version 2;
# see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

#include "bootmii_ppc.h"
#include "ohci.h"
#include "irq.h"

#define gecko_printf printf
#define dma_addr(address) virt_to_phys(address)


static struct ohci_hcca hcca_oh0;

static void dbg_op_state() {
       switch (read32(OHCI0_HC_CONTROL) & OHCI_CTRL_HCFS) {
               case OHCI_USB_SUSPEND:
                       gecko_printf("ohci-- OHCI_USB_SUSPEND\n");
                       break;
               case OHCI_USB_RESET:
                       gecko_printf("ohci-- OHCI_USB_RESET\n");
                       break;
               case OHCI_USB_OPER:
                       gecko_printf("ohci-- OHCI_USB_OPER\n");
                       break;
               case OHCI_USB_RESUME:
                       gecko_printf("ohci-- OHCI_USB_RESUME\n");
                       break;
       }
}

void ohci_init() {
       gecko_printf("ohci-- init\n");
       dbg_op_state();
       /*
       u32 i = 0;
       for(; i <= 0x200; i+=4) {
               gecko_printf("0x0d050000 + %X: %X\n", i, read32(0x0d050000+i));
               udelay(10000); //'cause usb gecko is lame
       }
       * see output in ohci.default
       */

       /* enable interrupts of both usb host controllers */
       set32(EHCI_CTL, EHCI_CTL_OH0INTE | EHCI_CTL_OH1INTE | 0xe0000);

       /* reset HC */
       set32(OHCI0_HC_COMMAND_STATUS, OHCI_HCR);

       /* wait max. 30us */
       u32 ts = 30;
       while ((read32(OHCI0_HC_COMMAND_STATUS) & OHCI_HCR) != 0) {
               if(--ts == 0) {
                       gecko_printf("ohci-- FAILED");
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
       write32(OHCI0_HC_HCCA, dma_addr(&hcca_oh0));

       /* set periodicstart */
#define FIT (1<<31)
       u32 fmInterval = read32(OHCI0_HC_FM_INTERVAL) &0x3fff;
       u32 fit = read32(OHCI0_HC_FM_INTERVAL) & FIT;

       write32(OHCI0_HC_FM_INTERVAL, read32(OHCI0_HC_FM_INTERVAL) | (fit ^ FIT));
       write32(OHCI0_HC_PERIODIC_START, ((9*fmInterval)/10)&0x3fff);

       /* testing bla */
       if ((read32(OHCI0_HC_FM_INTERVAL) & 0x3fff0000) == 0 || !read32(OHCI0_HC_PERIODIC_START)) {
               gecko_printf("ohci-- w00t, fail!! see ohci-hcd.c:669\n");
       }
       
       /* start HC operations */
       set32(OHCI0_HC_CONTROL, OHCI_CONTROL_INIT | OHCI_USB_OPER);

       /* wake on ConnectStatusChange, matching external hubs */
       set32(OHCI0_HC_RH_STATUS, RH_HS_DRWE);

       /* Choose the interrupts we care about now, others later on demand */
       write32(OHCI0_HC_INT_STATUS, ~0);
       write32(OHCI0_HC_INT_ENABLE, OHCI_INTR_INIT);


       irq_restore(cookie);

       dbg_op_state();
}
