#include <conio.h>
#include "dsp.h"
#include "pit.h"

/* Sound Blaster DSP ports, relative to the BLASTER port.
 * Source: https://pdos.csail.mit.edu/6.828/2017/readings/hardware/SoundBlaster.pdf */
#define DSP_RESET       0x06
#define DSP_READ        0x0A
#define DSP_WRITE       0x0C    /* bit 7 busy */
#define DSP_READ_READY  0x0E    /* bit 7 data ready */

#define DSP_READY_BYTE  0xAA
#define DSP_CMD_VERSION 0xE1

/* A reset should answer within 100 us, we give it more time */
#define DSP_TIMEOUT     PIT_US_TO_CLK(10000UL)

int dsp_write(uint16_t base, uint8_t val) {
    uint32_t start = pit_now();

    while (inp(base + DSP_WRITE) & 0x80)
        if ((uint32_t)(pit_now() - start) > DSP_TIMEOUT)
            return 0;
    outp(base + DSP_WRITE, val);
    return 1;
}

/* -1 on timeout */
int dsp_read(uint16_t base) {
    uint32_t start = pit_now();

    while (!(inp(base + DSP_READ_READY) & 0x80))
        if ((uint32_t)(pit_now() - start) > DSP_TIMEOUT)
            return -1;
    return inp(base + DSP_READ);
}

/* Pulse reset for at least 3 us, then the DSP answers 0xAA */
int dsp_reset(uint16_t base) {
    outp(base + DSP_RESET, 1);
    pit_delay_us(5);
    outp(base + DSP_RESET, 0);
    return dsp_read(base) == DSP_READY_BYTE;
}

/* =========================
 * Current Replies:
 * =========================
 * SB 2.0 (CT1350B)     2.02
 * SB16                 4.05
 * Vibra 16             4.13
 * Audician 32 Plus     3.01
 * ES1869               3.01
 * ESS Solo-1           3.01
 */
int dsp_get_version(uint16_t base, dsp_version_t *ver) {
    int major, minor;

    if (!dsp_write(base, DSP_CMD_VERSION))
        return 0;
    major = dsp_read(base);
    minor = dsp_read(base);
    if (major < 0 || minor < 0)
        return 0;
    ver->major = (uint8_t)major;
    ver->minor = (uint8_t)minor;
    return 1;
}
