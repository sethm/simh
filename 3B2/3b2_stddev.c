/* 3b2_stddev.h: AT&T 3B2 Common System Devices implementation

   Copyright (c) 2017, Seth J. Morabito

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Except as contained in this notice, the name of the author shall
   not be used in advertising or otherwise to promote the sale, use or
   other dealings in this Software without prior written authorization
   from the author.
*/

/*
   This file contains system-specific registers and devices for the
   following 3B2 devices:

   - timer       8253 interval timer
   - nvram       Non-Volatile RAM
   - tod         MM58174A / MM58374A Real-Time-Clock
*/

#include <3b2_rev3_defs.h>
#include <time.h>

#include "3b2_defs.h"
#include "3b2_stddev.h"

DEBTAB sys_deb_tab[] = {
    { "INIT",       INIT_MSG,       "Init"              },
    { "READ",       READ_MSG,       "Read activity"     },
    { "WRITE",      WRITE_MSG,      "Write activity"    },
    { "EXECUTE",    EXECUTE_MSG,    "Execute activity"  },
    { "IRQ",        IRQ_MSG,        "Interrupt activity"},
    { "TRACE",      TRACE_DBG,      "Detailed activity" },
    { NULL,         0                                   }
};

struct timer_ctr TIMERS[3];

uint32 *NVRAM = NULL;

int32 tmxr_poll = 16667;

UNIT nvram_unit = {
    UDATA(NULL, UNIT_FIX+UNIT_BINK, NVRSIZE)
};

REG nvram_reg[] = {
    { NULL }
};

DEVICE nvram_dev = {
    "NVRAM", &nvram_unit, nvram_reg, NULL,
    1, 16, 8, 4, 16, 32,
    &nvram_ex, &nvram_dep, &nvram_reset,
    NULL, &nvram_attach, &nvram_detach,
    NULL, DEV_DEBUG, 0, sys_deb_tab, NULL, NULL,
    &nvram_help, NULL, NULL,
    &nvram_description
};

t_stat nvram_ex(t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
    uint32 addr = (uint32) exta;

    if ((vptr == NULL) || (addr & 03)) {
        return SCPE_ARG;
    }

    if (addr >= NVRSIZE) {
        return SCPE_NXM;
    }

    *vptr = NVRAM[addr >> 2];

    return SCPE_OK;
}

t_stat nvram_dep(t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
    uint32 addr = (uint32) exta;

    if (addr & 03) {
        return SCPE_ARG;
    }

    if (addr >= NVRSIZE) {
        return SCPE_NXM;
    }

    NVRAM[addr >> 2] = (uint32) val;

    return SCPE_OK;
}

t_stat nvram_reset(DEVICE *dptr)
{
    if (NVRAM == NULL) {
        NVRAM = (uint32 *)calloc(NVRSIZE >> 2, sizeof(uint32));
        memset(NVRAM, 0, sizeof(uint32) * NVRSIZE >> 2);
        nvram_unit.filebuf = NVRAM;
    }

    if (NVRAM == NULL) {
        return SCPE_MEM;
    }

    return SCPE_OK;
}

const char *nvram_description(DEVICE *dptr)
{
    return "Non-volatile memory, used to store system state between boots.\n";
}

t_stat nvram_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st,
            "The NVRAM holds system state between boots. On initial startup,\n"
            "if no valid NVRAM file is attached, you will see the message:\n"
            "\n"
            "     FW ERROR 1-01: NVRAM SANITY FAILURE\n"
            "     DEFAULT VALUES ASSUMED\n"
            "     IF REPEATED, CHECK THE BATTERY\n"
            "\n"
            "To avoid this message on subsequent boots, attach a new NVRAM file\n"
            "with the SIMH command:\n"
            "\n"
            "     sim> ATTACH NVRAM <filename>\n");
    return SCPE_OK;
}

t_stat nvram_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;

    /* If we've been asked to attach, make sure the ATTABLE
       and BUFABLE flags are set on the unit */
    uptr->flags = uptr->flags | (UNIT_ATTABLE | UNIT_BUFABLE);

    r = attach_unit(uptr, cptr);

    if (r != SCPE_OK) {
        /* Unset the ATTABLE and BUFABLE flags if we failed. */
        uptr->flags = uptr->flags & (uint32) ~(UNIT_ATTABLE | UNIT_BUFABLE);
    } else {
        uptr->hwmark = (uint32) uptr->capac;
    }

    return r;
}

t_stat nvram_detach(UNIT *uptr)
{
    t_stat r;

    r = detach_unit(uptr);

    if ((uptr->flags & UNIT_ATT) == 0) {
        uptr->flags = uptr->flags & (uint32) ~(UNIT_ATTABLE | UNIT_BUFABLE);
    }

    return r;
}


uint32 nvram_read(uint32 pa, size_t size)
{
    uint32 offset = pa & 0xfff;
    uint32 data = 0;
    uint32 sc = (~(offset & 3) << 3) & 0x1f;

    switch(size) {
    case 8:
        data = (NVRAM[offset >> 2] >> sc) & BYTE_MASK;
        break;
    case 16:
        if (offset & 2) {
            data = NVRAM[offset >> 2] & HALF_MASK;
        } else {
            data = (NVRAM[offset >> 2] >> 16) & HALF_MASK;
        }
        break;
    case 32:
        data = NVRAM[offset >> 2];
        break;
    }

    return data;
}

void nvram_write(uint32 pa, uint32 val, size_t size)
{
    uint32 offset = pa & 0xfff;
    uint32 index = offset >> 2;
    uint32 sc, mask;

    switch(size) {
    case 8:
        sc = (~(pa & 3) << 3) & 0x1f;
        mask = (uint32) (0xff << sc);
        NVRAM[index] = (NVRAM[index] & ~mask) | (val << sc);
        break;
    case 16:
        if (offset & 2) {
            NVRAM[index] = (NVRAM[index] & ~HALF_MASK) | val;
        } else {
            NVRAM[index] = (NVRAM[index] & HALF_MASK) | (val << 16);
        }
        break;
    case 32:
        NVRAM[index] = val;
        break;
    }
}

/*
 * 82C53/82C54 Timer.
 *
 * The 82C53 (Rev2)/82C54 (Rev3) Timer IC has three interval timers,
 * which we treat here as three units.
 *
 * In the 3B2, the three timers are assigned specific purposes:
 *
 *  - Timer 0: SYSTEM SANITY TIMER. This timer is normally loaded with
 *             a short timeout and allowed to run. If it times out, it
 *             will generate an interrupt and cause a system
 *             error. Software resets the timer regularly to ensure
 *             that it does not time out.  It is fed by a 10 kHz
 *             clock, so each single counting step of this timer is
 *             100 microseconds.
 *
 *  - Timer 1: UNIX INTERVAL TIMER. This is the main timer that drives
 *             process switching in Unix. It operates at a fixed rate,
 *             and the counter is set up by Unix to generate an
 *             interrupt once every 10 milliseconds. The timer is fed
 *             by a 100 kHz clock, so each single counting step of
 *             this timer is 10 microseconds.
 *
 *  - Timer 2: BUS TIMEOUT TIMER. This timer is reset every time the
 *             IO bus is accessed, and then stopped when the IO bus
 *             responds. It is mainly used to determine when the IO
 *             bus is hung (e.g., no card is installed in a given
 *             slot, so nothing can respond). When it times out, it
 *             generates an interrupt. It is fed by a 500 kHz clock,
 *             so each single counting step of this timer is 2
 *             microseconds.
 *
 *
 * Implementaiton Notes
 * ====================
 *
 * In general, no attempt has been made to create an accurate
 * emulation of the 82C53/82C54 timer. This implementation is truly
 * built for the 3B2, and even more specifically for System V Unix,
 * which is the only operating system ever to have been ported to the
 * 3B2.
 *
 *  - The Bus Timeout Timer is not implemented other than a stub that
 *    is designed to pass hardware diagnostics. The simulator IO
 *    subsystem always sets the correct interrupt directly if the bus
 *    will not respond.
 *
 *  - The System Sanity Timer is also not implemented other than a
 *    stub to pass diagnostics.
 *
 *  - The main Unix Interval Timer is implemented as a true SIMH clock
 *    when set up for the correct mode. In other modes, it likewise
 *    implements a stub designed to pass diagnostics.
 */

UNIT timer_unit[] = {
    { UDATA(&timer0_svc, 0, 0) },
    { UDATA(&timer1_svc, UNIT_IDLE, 0) },
    { UDATA(&timer2_svc, 0, 0) },
    { NULL }
};

UNIT *timer_clk_unit = &timer_unit[1];

REG timer_reg[] = {
    { HRDATAD(DIV0,   TIMERS[0].divider, 16, "Divider (0)") },
    { HRDATAD(COUNT0, TIMERS[0].val,     16, "Count (0)")   },
    { HRDATAD(CTRL0,  TIMERS[0].ctrl,    8,  "Control (0)") },
    { HRDATAD(DIV1,   TIMERS[1].divider, 16, "Divider (1)") },
    { HRDATAD(COUNT1, TIMERS[1].val,     16, "Count (1)")   },
    { HRDATAD(CTRL1,  TIMERS[1].ctrl,    8,  "Control (1)") },
    { HRDATAD(DIV2,   TIMERS[2].divider, 16, "Divider (2)") },
    { HRDATAD(COUNT2, TIMERS[2].val,     16, "Count (2)")   },
    { HRDATAD(CTRL2,  TIMERS[2].ctrl,    8,  "Control (2)") },
    { NULL }
};

DEVICE timer_dev = {
    "TIMER", timer_unit, timer_reg, NULL,
    1, 16, 8, 4, 16, 32,
    NULL, NULL, &timer_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

t_stat timer_reset(DEVICE *dptr) {
    int32 i, t;

    memset(&TIMERS, 0, sizeof(struct timer_ctr) * 3);

    for (i = 0; i < 3; i++) {
        timer_unit[i].tmrnum = i;
        timer_unit[i].tmr = (void *)&TIMERS[i];
    }

    /* TODO: I don't think this is right. Verify. */
    /*
    if (!sim_is_running) {
        t = sim_rtcn_init_unit(timer_clk_unit, TPS_CLK, TMR_CLK);
        sim_activate_after_abs(timer_clk_unit, 1000000 / t);
    }
    */

    return SCPE_OK;
}

static void timer_activate(uint8 ctrnum)
{
    struct timer_ctr *ctr;

    ctr = &TIMERS[ctrnum];

    switch (ctrnum) {
    case TIMER_SANITY:
        if ((csr_data & CSRISTIM) == 0) {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] SANITY TIMER: Activating after %d steps\n",
                      R[NUM_PC], ctr->val);
            sim_activate_abs(&timer_unit[ctrnum], ctr->val);
            ctr->val--;
        } else {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] SANITY TIMER: Currently disabled, not starting\n",
                      R[NUM_PC]);
        }
        break;
    case TIMER_INTERVAL:
        if ((csr_data & CSRITIM) == 0) {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] INTERVAL TIMER: Activating after %d ms\n",
                      R[NUM_PC], ctr->val);
            sim_activate_after_abs(&timer_unit[ctrnum], ctr->val);
            ctr->val--;
        } else {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] INTERVAL TIMER: Currently disabled, not starting\n",
                      R[NUM_PC]);
        }
        break;
    case TIMER_BUS:
        if ((csr_data & CSRITIMO) == 0) {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] BUS TIMER: Activating after %d steps\n",
                      R[NUM_PC], ctr->val);
            sim_activate_abs(&timer_unit[ctrnum], (ctr->val - 2));
            ctr->val -= 2;
        } else {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] BUS TIMER: Currently disabled, not starting\n",
                      R[NUM_PC]);
        }
        break;
    default:
        break;
    }
}

void timer_enable(uint8 ctrnum)
{
    timer_activate(ctrnum);
}

void timer_disable(uint8 ctrnum)
{
    sim_debug(EXECUTE_MSG, &timer_dev,
              "[%08x] Disabling timer %d\n",
              R[NUM_PC], ctrnum);
    sim_cancel(&timer_unit[ctrnum]);
}

/*
 * Sanity Timer
 */
t_stat timer0_svc(UNIT *uptr)
{
    struct timer_ctr *ctr;
    int32 time;

    ctr = (struct timer_ctr *)uptr->tmr;

#if defined (REV3)
    if (ctr->enabled) {
        sim_debug(EXECUTE_MSG, &timer_dev,
                  "[%08x] TIMER 0 COMPLETION.\n",
                  R[NUM_PC]);
        if (!(csr_data & CSRISTIM)) {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] TIMER 0 NMI IRQ.\n",
                      R[NUM_PC]);
            ctr->val = 0xffff;
            cpu_nmi = TRUE;
            CSRBIT(CSRSTIMO, TRUE);
        }
    }
#endif

    return SCPE_OK;
}

/*
 * Interval Timer
 */
t_stat timer1_svc(UNIT *uptr)
{
    struct timer_ctr *ctr;
    int32 t;

    ctr = (struct timer_ctr *)uptr->tmr;

    if (ctr->enabled && !(csr_data & CSRITIM)) {
        /* Fire the IPL 15 clock interrupt */
        sim_debug(EXECUTE_MSG, &timer_dev,
                  ">>> timer1_svc!\n");
        CSRBIT(CSRCLK, TRUE);
    }

    t = sim_rtcn_calb(TPS_CLK, TMR_CLK);
    sim_activate_after_abs(uptr, 1000000/TPS_CLK);
    tmxr_poll = t;

    return SCPE_OK;
}

/*
 * Bus Timeout Timer
 */
t_stat timer2_svc(UNIT *uptr)
{
    struct timer_ctr *ctr;
    ctr = (struct timer_ctr *)uptr->tmr;

#if defined (REV3)
    if (ctr->enabled && TIMER_RW(ctr) == CLK_LSB) {
        sim_debug(EXECUTE_MSG, &timer_dev,
                  "[%08x] TIMER 2 COMPLETION.\n",
                  R[NUM_PC]);
        if (!(csr_data & CSRITIMO)) {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] TIMER 2 IRQ.\n",
                      R[NUM_PC]);
            ctr->val = 0xffff;
            CSRBIT(CSRTIMO, TRUE);
            /* Also trigger a bus abort */
            cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
        }
    }
#endif

    return SCPE_OK;
}

uint32 timer_read(uint32 pa, size_t size)
{
    uint32 reg;
    uint16 ctr_val;
    uint8 ctrnum, retval;
    struct timer_ctr *ctr;

    reg = pa - TIMERBASE;
    ctrnum = (reg >> 2) & 0x3;
    ctr = &TIMERS[ctrnum];

    switch (reg) {
    case TIMER_REG_DIVA:
    case TIMER_REG_DIVB:
    case TIMER_REG_DIVC:
        ctr_val = ctr->val;

        switch (TIMER_RW(ctr)) {
        case CLK_LSB:
            retval = ctr_val & 0xff;
            sim_debug(READ_MSG, &timer_dev,
                      "[%08x] [%d] [LSB] val=%d (0x%x)\n",
                      R[NUM_PC], ctrnum, retval, retval);
            break;
        case CLK_MSB:
            retval = (ctr_val & 0xff00) >> 8;
            sim_debug(READ_MSG, &timer_dev,
                      "[%08x] [%d] [MSB] val=%d (0x%x)\n",
                      R[NUM_PC], ctrnum, retval, retval);
            break;
        case CLK_LMB:
            if (ctr->r_ctrl_latch) {
                ctr->r_ctrl_latch = FALSE;
                retval = ctr->ctrl_latch;
                sim_debug(READ_MSG, &timer_dev,
                          "[%08x] [%d] [LATCH CTRL] val=%d (0x%x)\n",
                          R[NUM_PC], ctrnum, retval, retval);
            } else if (ctr->r_cnt_latch) {
                if (ctr->r_lmb) {
                    ctr->r_lmb = FALSE;
                    retval = (ctr->cnt_latch & 0xff00) >> 8;
                    ctr->r_cnt_latch = FALSE;
                    sim_debug(READ_MSG, &timer_dev,
                              "[%08x] [%d] [LATCH DATA MSB] val=%d (0x%x)\n",
                              R[NUM_PC], ctrnum, retval, retval);
                } else {
                    ctr->r_lmb = TRUE;
                    retval = ctr->cnt_latch & 0xff;
                    sim_debug(READ_MSG, &timer_dev,
                              "[%08x] [%d] [LATCH DATA LSB] val=%d (0x%x)\n",
                              R[NUM_PC], ctrnum, retval, retval);
                }
            } else if (ctr->r_lmb) {
                ctr->r_lmb = FALSE;
                retval = (ctr_val & 0xff00) >> 8;
                sim_debug(READ_MSG, &timer_dev,
                          "[%08x] [%d] [LMB - MSB] val=%d (0x%x)\n",
                          R[NUM_PC], ctrnum, retval, retval);
            } else {
                ctr->r_lmb = TRUE;
                retval = ctr_val & 0xff;
                sim_debug(READ_MSG, &timer_dev,
                          "[%08x] [%d] [LMB - LSB] val=%d (0x%x)\n",
                          R[NUM_PC], ctrnum, retval, retval);
            }
            break;
        default:
            retval = 0;
        }

        return retval;
    case TIMER_REG_CTRL:
        return ctr->ctrl;
    case TIMER_CLR_LATCH:
        /* Clearing the timer latch has a side-effect
           of also clearing pending interrupts */
        CSRBIT(CSRCLK, FALSE);
        return 0;
    default:
        /* Unhandled */
        sim_debug(READ_MSG, &timer_dev,
                  "[%08x] UNHANDLED TIMER READ. ADDR=%08x\n",
                  R[NUM_PC], pa);
        return 0;
    }
}

static void handle_timer_write(uint8 ctrnum, uint32 val)
{
    struct timer_ctr *ctr;
    UNIT *unit = &timer_unit[ctrnum];

    ctr = &TIMERS[ctrnum];
    ctr->enabled = TRUE;

    switch(TIMER_RW(ctr)) {
    case CLK_LSB:
        ctr->divider = val & 0xff;
        ctr->val = ctr->divider;
        sim_debug(WRITE_MSG, &timer_dev,
                  "[%08x] [%d] [LSB] val=%d (0x%x)\n",
                  R[NUM_PC], ctrnum, val & 0xff, val & 0xff);
        timer_activate(ctrnum);
        break;
    case CLK_MSB:
        ctr->divider = (val & 0xff) << 8;
        ctr->val = ctr->divider;
        sim_debug(WRITE_MSG, &timer_dev,
                  "[%08x] [%d] [MSB] val=%d (0x%x)\n",
                  R[NUM_PC], ctrnum, val & 0xff, val & 0xff);
        timer_activate(ctrnum);
        break;
    case CLK_LMB:
        if (ctr->w_lmb) {
            ctr->w_lmb = FALSE;
            ctr->divider = (uint16) ((ctr->divider & 0x00ff) | ((val & 0xff) << 8));
            ctr->val = ctr->divider;
            sim_debug(WRITE_MSG, &timer_dev,
                      "[%08x] [%d] [LMB - MSB] val=%d (0x%x)\n",
                      R[NUM_PC], ctrnum, val & 0xff, val & 0xff);
            timer_activate(ctrnum);
        } else {
            ctr->w_lmb = TRUE;
            ctr->divider = (ctr->divider & 0xff00) | (val & 0xff);
            ctr->val = ctr->divider;
            sim_debug(WRITE_MSG, &timer_dev,
                      "[%08x] [%d] [LMB - LSB] val=%d (0x%x)\n",
                      R[NUM_PC], ctrnum, val & 0xff, val & 0xff);
        }
        break;
    default:
        break;

    }
}

void timer_write(uint32 pa, uint32 val, size_t size)
{
    uint8 reg, ctrnum;
    struct timer_ctr *ctr;

    reg = (uint8) (pa - TIMERBASE);

    switch(reg) {
    case TIMER_REG_DIVA:
        handle_timer_write(0, val);
        break;
    case TIMER_REG_DIVB:
        handle_timer_write(1, val);
        break;
    case TIMER_REG_DIVC:
        handle_timer_write(2, val);
        break;
    case TIMER_REG_CTRL:
        /* The counter number is in bits 6 and 7 */
        ctrnum = (val >> 6) & 3;
        if (ctrnum == 3) {
            sim_debug(WRITE_MSG, &timer_dev,
                      "[%08x] READ BACK COMMAND. DATA=%02x\n",
                      R[NUM_PC], val);
            if (val & 2) {
                ctr = &TIMERS[0];
                if ((val & 0x20) == 0) {
                    ctr->ctrl_latch = (uint16) TIMERS[2].ctrl;
                    ctr->r_ctrl_latch = TRUE;
                }
                if ((val & 0x20) == 0) {
                    ctr->cnt_latch = ctr->val;
                    ctr->r_cnt_latch = TRUE;
                }
            }
            if (val & 4) {
                ctr = &TIMERS[1];
                if ((val & 0x10) == 0) {
                    ctr->ctrl_latch = (uint16) TIMERS[2].ctrl;
                    ctr->r_ctrl_latch = TRUE;
                }
                if ((val & 0x20) == 0) {
                    ctr->cnt_latch = ctr->val;
                    ctr->r_cnt_latch = TRUE;
                }
            }
            if (val & 8) {
                ctr = &TIMERS[2];
                if ((val & 0x10) == 0) {
                    ctr->ctrl_latch = (uint16) TIMERS[2].ctrl;
                    ctr->r_ctrl_latch = TRUE;
                }
                if ((val & 0x20) == 0) {
                    ctr->cnt_latch = ctr->val;
                    ctr->r_cnt_latch = TRUE;
                }
            }
        } else {
            sim_debug(WRITE_MSG, &timer_dev,
                      "[%08x] Timer Control Write: timer %d => %02x\n",
                      R[NUM_PC], ctrnum, val & 0xff);
            ctr = &TIMERS[ctrnum];
            ctr->ctrl = (uint8) val;
            ctr->enabled = FALSE;
            ctr->w_lmb = FALSE;
            ctr->r_lmb = FALSE;
            ctr->val = 0xffff;
            ctr->divider = 0xffff;
        }
        break;
    case TIMER_CLR_LATCH:
        sim_debug(WRITE_MSG, &timer_dev,
                  "[%08x] unexpected write to clear timer latch\n",
                  R[NUM_PC]);
        break;
    default:
        sim_debug(WRITE_MSG, &timer_dev,
                  "[%08x] unknown timer register: %d\n",
                  R[NUM_PC], reg);
    }
}

/*
 * MM58174A Time Of Day Clock (Rev 2) /
 * MM58274C Time Of Day Clock (Rev 3)
 *
 * This is a battery-backed real time clock used to store the current
 * date and time between boots. It is set when an operator changes the
 * date and time. Is is read at boot time.
 */

UNIT tod_unit = {
    UDATA(NULL, UNIT_FIX+UNIT_BINK, sizeof(TOD_DATA))
};

DEVICE tod_dev = {
    "TOD", &tod_unit, NULL, NULL,
    1, 16, 8, 4, 16, 32,
    NULL, NULL, &tod_reset,
    NULL, &tod_attach, &tod_detach,
    NULL, DEV_DEBUG, 0, sys_deb_tab, NULL, NULL,
    &tod_help, NULL, NULL,
    &tod_description
};

t_stat tod_reset(DEVICE *dptr)
{
    if (tod_unit.filebuf == NULL) {
        tod_unit.filebuf = calloc(sizeof(TOD_DATA), 1);
        if (tod_unit.filebuf == NULL) {
            return SCPE_MEM;
        }
    }

    return SCPE_OK;
}

t_stat tod_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;

    uptr->flags = uptr->flags | (UNIT_ATTABLE | UNIT_BUFABLE);

    r = attach_unit(uptr, cptr);

    if (r != SCPE_OK) {
        uptr->flags = uptr->flags & (uint32) ~(UNIT_ATTABLE | UNIT_BUFABLE);
    } else {
        uptr->hwmark = (uint32) uptr->capac;
    }

    return r;
}

t_stat tod_detach(UNIT *uptr)
{
    t_stat r;

    r = detach_unit(uptr);

    if ((uptr->flags & UNIT_ATT) == 0) {
        uptr->flags = uptr->flags & (uint32) ~(UNIT_ATTABLE | UNIT_BUFABLE);
    }

    return r;
}

/*
 * Re-set the tod_data registers based on the current simulated time.
 */
void tod_resync()
{
    struct timespec now;
    struct tm tm;
    time_t sec;
    TOD_DATA *td = (TOD_DATA *)tod_unit.filebuf;

    sim_rtcn_get_time(&now, TMR_CLK);
    sec = now.tv_sec - td->delta;

    /* Populate the tm struct based on current sim_time */
    tm = *gmtime(&sec);

    td->tsec = 0;
    td->unit_sec = tm.tm_sec % 10;
    td->ten_sec = tm.tm_sec / 10;
    td->unit_min = tm.tm_min % 10;
    td->ten_min = tm.tm_min / 10;
    td->unit_hour = tm.tm_hour % 10;
    td->ten_hour = tm.tm_hour / 10;
    /* tm struct stores as 0-11, tod struct as 1-12 */
    td->unit_mon = (tm.tm_mon + 1) % 10;
    td->ten_mon = (tm.tm_mon + 1) / 10;
    td->unit_day = tm.tm_mday % 10;
    td->ten_day = tm.tm_mday / 10;
#ifdef REV2
    td->year = 1 << ((tm.tm_year - 1) % 4);
#endif
}

/*
 * Re-calculate the delta between real time and simulated time
 */
void tod_update_delta()
{
    struct timespec now;
    struct tm tm = {0};
    time_t ssec;
    TOD_DATA *td = (TOD_DATA *)tod_unit.filebuf;
    sim_rtcn_get_time(&now, TMR_CLK);

    /* Let the host decide if it is DST or not */
    tm.tm_isdst = -1;

    /* Compute the simulated seconds value */
    tm.tm_sec = (td->ten_sec * 10) + td->unit_sec;
    tm.tm_min = (td->ten_min * 10) + td->unit_min;
    tm.tm_hour = (td->ten_hour * 10) + td->unit_hour;
    /* tm struct stores as 0-11, tod struct as 1-12 */
    tm.tm_mon = ((td->ten_mon * 10) + td->unit_mon) - 1;
    tm.tm_mday = (td->ten_day * 10) + td->unit_day;

#ifdef REV2
    /* We're forced to do this weird arithmetic because the TOD chip
     * used by the 3B2 does not store the year. It only stores the
     * offset from the nearest leap year. */
    switch(td->year) {
    case 1: /* Leap Year - 3 */
        tm.tm_year = 85;
        break;
    case 2: /* Leap Year - 2 */
        tm.tm_year = 86;
        break;
    case 4: /* Leap Year - 1 */
        tm.tm_year = 87;
        break;
    case 8: /* Leap Year */
        tm.tm_year = 88;
        break;
    default:
        break;
    }
#else
    tm.tm_year = (td->ten_year * 10) + td->year;
    sim_debug(EXECUTE_MSG, &tod_dev,
              "[%08x] Updating TOD delta. td->ten_year=%d td->year=%d tm_year=%d\n",
              R[NUM_PC], td->ten_year, td->year, tm.tm_year);
#endif

    ssec = mktime(&tm);
    td->delta = (int32)(now.tv_sec - ssec);
}

uint32 tod_read(uint32 pa, size_t size)
{
    uint8 reg;
    TOD_DATA *td = (TOD_DATA *)(tod_unit.filebuf);

    tod_resync();

    reg = pa - TODBASE;

    sim_debug(READ_MSG, &tod_dev,
              "[%08x] TOD: reg=%02x\n",
              R[NUM_PC], reg);

    switch(reg) {
    case 0x04:        /* 1/10 Sec    */
        return td->tsec;
    case 0x08:        /* 1 Sec       */
        return td->unit_sec;
    case 0x0c:        /* 10 Sec      */
        return td->ten_sec;
    case 0x10:        /* 1 Min       */
        return td->unit_min;
    case 0x14:        /* 10 Min      */
        return td->ten_min;
    case 0x18:        /* 1 Hour      */
        return td->unit_hour;
    case 0x1c:        /* 10 Hour     */
        return td->ten_hour;
    case 0x20:        /* 1 Day       */
        return td->unit_day;
    case 0x24:        /* 10 Day      */
        return td->ten_day;
    case 0x28:        /* Day of Week */
#ifdef REV2
        return td->wday;
#else
        return td->unit_mon;
#endif
    case 0x2c:        /* 1 Month     */
#ifdef REV2
        return td->unit_mon;
#else
        return td->ten_mon;
#endif
    case 0x30:        /* 10 Month    */
#ifdef REV2
        return td->ten_mon;
#else
        return td->year;
#endif
    case 0x34:        /* Year        */
#ifdef REV2
        return td->year;
#else
        return td->ten_year;
#endif
    default:
        break;
    }

    return 0;
}

void tod_write(uint32 pa, uint32 val, size_t size)
{
    uint32 reg;
    TOD_DATA *td = (TOD_DATA *)(tod_unit.filebuf);

    reg = pa - TODBASE;

    sim_debug(WRITE_MSG, &tod_dev,
              "[%08x] TOD: reg=%02x val=%02x\n",
              R[NUM_PC], reg, (uint8) val);


    switch(reg) {
    case 0x00:        /* Test / Control */
#ifdef REV3
        if (val & 1) {
            tod_update_delta();
        }
#endif
        break;
    case 0x04:        /* 1/10 Sec    */
        td->tsec = (uint8) val;
        break;
    case 0x08:        /* 1 Sec       */
        td->unit_sec = (uint8) val;
        break;
    case 0x0c:        /* 10 Sec      */
        td->ten_sec = (uint8) val;
        break;
    case 0x10:        /* 1 Min       */
        td->unit_min = (uint8) val;
        break;
    case 0x14:        /* 10 Min      */
        td->ten_min = (uint8) val;
        break;
    case 0x18:        /* 1 Hour      */
        td->unit_hour = (uint8) val;
        break;
    case 0x1c:        /* 10 Hour     */
        td->ten_hour = (uint8) val;
        break;
    case 0x20:        /* 1 Day       */
        td->unit_day = (uint8) val;
        break;
    case 0x24:        /* 10 Day      */
        td->ten_day = (uint8) val;
        break;
    case 0x28:        /* Day of Week (Rev 2) / 1 Month (Rev 3) */
#ifdef REV2
        td->wday = (uint8) val;
#else
        td->unit_mon = (uint8) val;
#endif
        break;
    case 0x2c:        /* 1 Month (Rev 2) / 10 Month (Rev 3) */
#ifdef REV2
        td->unit_mon = (uint8) val;
#else
        td->ten_mon = (uint8) val;
#endif
        break;
    case 0x30:        /* 10 Month (Rev 2) / Year (Rev 3) */
#ifdef REV2
        td->ten_mon = (uint8) val;
#else
        td->year = (uint8) val;
#endif
        break;
    case 0x34:        /* Year (Rev 2) / 10 Year (Rev 3) */
#ifdef REV2
        td->year = (uint8) val;
#else
        td->ten_year = (uint8) val;
#endif
        break;
    case 0x38:        /* Stop / Start */
        if (val & 1) {
            tod_update_delta();
        }
        break;
    case 0x3c:        /* Clock Setting / Interrupt */
        break;
    default:
        break;
    }
}

const char *tod_description(DEVICE *dptr)
{
    return "Time-of-Day clock, used to store system time between boots.\n";
}

t_stat tod_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st,
            "The TOD is a battery-backed time-of-day clock that holds system\n"
            "time between boots. In order to store the time, a file must be\n"
            "attached to the TOD device with the SIMH command:\n"
            "\n"
            "     sim> ATTACH TOD <filename>\n"
            "\n"
            "On a newly installed System V Release 3 UNIX system, no system\n"
            "time will be stored in the TOD clock. In order to set the system\n"
            "time, run the following command from within UNIX (as root):\n"
            "\n"
            "     # sysadm datetime\n"
            "\n"
            "On subsequent boots, the correct system time will restored from\n"
            "from the TOD.\n");

    return SCPE_OK;
}

#if defined(REV3)

/*
 * Fault Register
 *
 * The Fault Register is composed of two 32-bit registers at addresses
 * 0x4C000 and 0x4D000. These latch state of the last address to cause
 * a CPU fault.
 *
 *   Bits 00-25: Physical memory address bits 00-25
 */

uint32 flt_1 = 0;
uint32 flt_2 = 0;

UNIT flt_unit = {
    UDATA(NULL, UNIT_FIX+UNIT_BINK, 8)
};

REG flt_reg[] = {
    { NULL }
};

DEVICE flt_dev = {
    "FLT", &flt_unit, flt_reg, NULL,
    1, 16, 8, 4, 16, 32,
    NULL, NULL, NULL,
    NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, sys_deb_tab, NULL, NULL,
    NULL, NULL, NULL,
    NULL
};

uint32 flt_read(uint32 pa, size_t size)
{
    sim_debug(READ_MSG, &flt_dev,
              "[%08x] Read from FLT Register at %x\n",
              R[NUM_PC], pa);
    return 0;
}

void flt_write(uint32 pa, uint32 val, size_t size)
{
    sim_debug(WRITE_MSG, &flt_dev,
              "[%08x] Write to FLT Register at %x (val=%x)\n",
              R[NUM_PC], pa, val);
    return;
}

#endif
