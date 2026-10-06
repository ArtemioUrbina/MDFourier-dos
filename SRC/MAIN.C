/*
 *  MDFourier for DOS
 *  Released under the GNU GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <i86.h>
#include "carddrv.h"
#include "env.h"
#include "mixer.h"
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

int main(int argc, char *argv[]) {
    blaster_cfg_t cfg;
    double        hz;
    int           stereo, i, set_mixer = 1;

    printf("MDFourier DOS Artemio Urbina 2026\n");

    for (i = 1; i < argc; i++) {
        if ((argv[i][0] == '/' || argv[i][0] == '-') &&
            (argv[i][1] == 'K' || argv[i][1] == 'k') && !argv[i][2])
            set_mixer = 0;
        else {
            printf("Usage: MDF [/K]\n"
                   "  The mixer is set for capture and restored at exit\n"
                   "  /K  keep the current mixer settings\n");
            return 0;
        }
    }

    if (env_get_blaster(&cfg))
        print_env(&cfg);
    else
        printf("BLASTER: not set\n");

    if (windows_running()) {
        printf("Running under Windows is not supported, exit Windows first\n");
        return 0;
    }

    if (set_mixer) {
        if(mixer_set_standard(&cfg))
            printf("Mixer: levels changed for capture, will restore at exit\n");
        else
            printf("Mixer: settings not accepted, set your mixer manually\n");
        atexit(mixer_restore);
    }

    if(mixer_report(&cfg) == 0 && !set_mixer)
        printf("  NOTE: kept by /K, run without it to set the mixer for capture\n");

    pit_init();

    if (!vsync_calibrate(&hz)) {
        printf("No usable vertical retrace (VGA required)\n");
        return 0;
    }

    printf("Probing FM (%s)...", opl_driver.name);
    if(opl_driver.detect()) {
        printf("found (%s)\n", opl_is_opl3() ? "OPL3" : "OPL2");
        opl_driver.init();

        /* OPL3 and compatibles */
        stereo = opl_is_opl3();
        if (stereo)
            opl3_set_new_mode(1);

        printf("Playing MDFourier...\n");
        mdf_run_full_test(0, SWEEP_FRAMES, stereo);

        /* silence the chip */
        opl_driver.reset();

    } else
        printf("not found\n");

    return 0;
}
