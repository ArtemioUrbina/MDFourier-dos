/*
 *  MDFourier for DOS
 *  Released under the GNU GPL
 */

#include <stdio.h>
#include <i86.h>
#include "carddrv.h"
#include "env.h"
#include "opl.h"
#include "pit.h"
#include "vsync.h"
#include "opltone.h"
#include "mdf.h"

#define SWEEP_FRAMES 20

/* Windows virtualizes VGA, PIT and interrupts, so we check
 * INT 2Fh AX=1600h for 00h or 80h in AL when it is not running */
static int windows_running() {
    union REGS reg;

    reg.x.ax = 0x1600;
    int86(0x2F, &reg, &reg);
    return reg.h.al != 0x00 && reg.h.al != 0x80;
}

int main() {
    blaster_cfg_t cfg;
    double        hz;

    printf("MDFourier DOS Artemio Urbina 2026\n");
    if (env_get_blaster(&cfg))
        print_env(&cfg);
    else
        printf("BLASTER: not set\n");

    if (windows_running()) {
        printf("Running under Windows is not supported, exit Windows first\n");
        return 0;
    }

    pit_init();

    if (!vsync_calibrate(&hz)) {
        printf("No usable vertical retrace (VGA required)\n");
        return 0;
    }
    /* We don't print the preliminary for now */
    /* printf("  Refresh: %0.4fHz (%0.4fms per frame)\n", hz, 1000.0/hz); */

    printf("Probing FM (%s)...", opl_driver.name);
    if(opl_driver.detect()) {
        printf("found (%s)\n", opl_is_opl3() ? "OPL3" : "OPL2");
        opl_driver.init();

        printf("Playing MDFourier...\n");
        mdf_run_full_test(0, SWEEP_FRAMES);

        /* silence the chip */
        opl_driver.reset();

    } else
        printf("not found\n");

    return 0;
}
