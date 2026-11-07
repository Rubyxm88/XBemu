/*
 * QEMU Geforce NV2A implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2021 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "nv2a_int.h"

/* PMC - card master control */
uint64_t pmc_read(void *opaque, hwaddr addr, unsigned int size)
{
    NV2AState *d = (NV2AState *)opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PMC_BOOT_0:
        /* chipset and stepping:
         * NV2A, A03, Rev 0 */

        r = 0x02A000A3;
        break;
    case NV_PMC_INTR_0:
        /* Shows which functional units have pending IRQ */
        r = d->pmc.pending_interrupts;
        break;
    case NV_PMC_INTR_EN_0:
        /* Selects which functional units can cause IRQs */
        r = d->pmc.enabled_interrupts;
        break;
    default:
        break;
    }

    nv2a_reg_log_read(NV_PMC, addr, size, r);
    return r;
}

void pmc_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size)
{
    NV2AState *d = (NV2AState *)opaque;

    nv2a_reg_log_write(NV_PMC, addr, size, val);

    switch (addr) {
    case NV_PMC_INTR_0:
        /* the bits of the interrupts to clear are wrtten */
        d->pmc.pending_interrupts &= ~val;
        nv2a_update_irq(d);
        break;
    case NV_PMC_INTR_EN_0:
        d->pmc.enabled_interrupts = val;
        nv2a_update_irq(d);
        break;
    case NV_PMC_ENABLE:
        /*
         * Turning PFIFO off resets it. A title launched after an nxdk
         * program (XBL Dash) toggles this and then writes DMA_PUT = 0,
         * expecting DMA_GET and the pusher state to be back at reset.
         */
        if (!(val & NV_PMC_ENABLE_PFIFO)) {
            /* Only the DMA pusher state; the rest is left as it was. */
            qemu_mutex_lock(&d->pfifo.lock);
            d->pfifo.regs[NV_PFIFO_CACHE1_PUSH0] = 0;
            d->pfifo.regs[NV_PFIFO_CACHE1_DMA_PUSH] = 0;
            d->pfifo.regs[NV_PFIFO_CACHE1_DMA_STATE] = 0;
            d->pfifo.regs[NV_PFIFO_CACHE1_DMA_PUT] = 0;
            d->pfifo.regs[NV_PFIFO_CACHE1_DMA_GET] = 0;
            d->pfifo.regs[NV_PFIFO_CACHE1_DMA_DCOUNT] = 0;
            d->pfifo.regs[NV_PFIFO_CACHE1_DMA_SUBROUTINE] = 0;
            qemu_mutex_unlock(&d->pfifo.lock);
        }
        break;
    default:
        break;
    }
}

