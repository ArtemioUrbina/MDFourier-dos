
/*
 * VGA vertical retrace detection.
 *
 * Port 0x3DA, bit 3 (0x08) of the Input Status Register 1 is set
 * for the duration of the vertical retrace pulse.
 *
 */

#include <conio.h>
#include "vsync.h"

#define VGA_STATUS_PORT 0x3DA
#define VGA_VRETRACE    0x08

void vsync_wait() {
    while (inp(VGA_STATUS_PORT) & VGA_VRETRACE)
        ;   /* let any retrace already in progress finish */

    while (!(inp(VGA_STATUS_PORT) & VGA_VRETRACE))
        ;   /* wait for the next one to start */
}
