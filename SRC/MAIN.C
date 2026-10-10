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

/* Exit codes, for batch files (IF ERRORLEVEL n means n or higher) */
#define EXIT_OK         0   /* sequence complete, capture usable */
#define EXIT_USAGE      1   /* bad command line */
#define EXIT_WINDOWS    2   /* running under Windows */
#define EXIT_NO_VGA     3   /* no usable vertical retrace */
#define EXIT_NO_FM      4   /* no OPL2/OPL3 at 0x388 */
#define EXIT_ABORTED    5   /* ESC pressed */
#define EXIT_BAD_FRAMES 6   /* complete, frames out of tolerance: discard */

/* Windows virtualizes VGA, PIT and interrupts, so we check
 * INT 2Fh AX=1600h for 00h or 80h in AL when it is not running */
int windows_running(void) {
    union REGS reg;

    reg.x.ax = 0x1600;
    int86(0x2F, &reg, &reg);
    return reg.h.al != 0x00 && reg.h.al != 0x80;
}

int main(int argc, char *argv[]) {
    blaster_cfg_t cfg;
    double        hz;
    int           stereo, i, set_mixer = 1, mixer, mixer_ok, result;

    printf("MDFourier DOS Artemio Urbina 2026\n");

    for (i = 1; i < argc; i++) {
        if ((argv[i][0] == '/' || argv[i][0] == '-') &&
            (argv[i][1] == 'K' || argv[i][1] == 'k') && !argv[i][2])
            set_mixer = 0;
        else {
            printf("Usage: MDF [/K]\n"
                   "  The mixer is set for capture and restored at exit\n"
                   "  /K  keep the current mixer settings\n");
            return EXIT_USAGE;
        }
    }

    if (env_get_blaster(&cfg))
        print_env(&cfg);
    else
        printf("BLASTER: not set\n");

    if (windows_running()) {
        printf("Running under Windows is not supported, exit Windows first\n");
        return EXIT_WINDOWS;
    }

    if (set_mixer) {
        int mixer_saved = mixer_set_standard(&cfg);
        if(mixer_saved != -1) {
            if(mixer_saved)
                printf("Mixer: levels changed for capture, will restore at exit\n");
            else
                printf("Mixer: settings not accepted, set your mixer manually\n");
            atexit(mixer_restore);
        }
    }

    mixer = mixer_report(&cfg);
    if(mixer == MIXER_WARN && !set_mixer)
        printf("  NOTE: kept by /K, run without it to set the mixer for capture\n");

    /* Recorded in the watermark tone. No mixer chip counts as valid */
    mixer_ok = (mixer == MIXER_OK || mixer == MIXER_NONE);
    if(!mixer_ok)
        printf("Mixer: not verified, watermarking\n");

    pit_init();

    if (!vsync_calibrate(&hz)) {
        printf("No usable vertical retrace (VGA required)\n");
        return EXIT_NO_VGA;
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
        result = mdf_run_full_test(0, SWEEP_FRAMES, stereo, mixer_ok);

        /* silence the chip */
        opl_driver.reset();

    } else {
        printf("not found\n");
        return EXIT_NO_FM;
    }

    if (result == MDF_ABORTED)
        return EXIT_ABORTED;
    if (result == MDF_BAD_FRAMES)
        return EXIT_BAD_FRAMES;
    return EXIT_OK;
}
