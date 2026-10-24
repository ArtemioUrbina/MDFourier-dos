/*
 *  MDFourier for DOS
 *  Released under the GNU GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <i86.h>
#include "carddrv.h"
#include "dsp.h"
#include "pcm.h"
#include "env.h"
#include "mixer.h"
#include "sax.h"
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
#define EXIT_BAD_FRAMES 6   /* frame out of tolerance, discard */
#define EXIT_WSS        7   /* WSS codec found, /W needed */
#define EXIT_BAD_IRQ    8   /* the DSP raises another IRQ than BLASTER I */

/* Windows virtualizes VGA, PIT and interrupts, so we check
 * INT 2Fh AX=1600h for 00h or 80h in AL when it is not running */
int windows_running() {
    union REGS reg;

    reg.x.ax = 0x1600;
    int86(0x2F, &reg, &reg);
    return reg.h.al != 0x00 && reg.h.al != 0x80;
}

int main(int argc, char *argv[]) {
    blaster_cfg_t cfg;
    double        hz;
    int           stereo, i, set_mixer = 1, report_only = 0, mixer, mixer_ok, result;
    int           bad_option = 0;
    char          *wss_ignored = NULL;
    int           sbpro_compatible = 0;

    printf("MDFourier DOS 0.952 [240p Test Suite Fourier Audio compare tool]\n");
    printf("Artemio Urbina 2026 free software under GPL http://junkerhq.net/MDFourier\n");

    for (i = 1; i < argc; i++) {
        char opt = 0;

        if ((argv[i][0] == '/' || argv[i][0] == '-') && argv[i][1] && !argv[i][2])
            opt = argv[i][1];
        if ((argv[i][0] == '/' || argv[i][0] == '-') &&
            (argv[i][1] == 'W' || argv[i][1] == 'w') && argv[i][2] == ':') {
            if (!env_parse_wss(argv[i] + 3))
                bad_option = 1;
        } else if (opt == 'K' || opt == 'k')
            set_mixer = 0;
        else if (opt == 'R' || opt == 'r')
            report_only = 1;
        else if (opt == 'V' || opt == 'v')
            verbose = 1;
        else
            bad_option = 1;
    }

    if (bad_option) {
        printf("Usage: MDF [/K] [/R] [/V] [/W:AUTO | /W:OFF | /W:port,irq,dma]\n"
               "  Needs a 486SX or faster with internal cache enabled\n"
               "  The mixer levels are set for playback and restored at exit\n"
               "  /K  keep the current mixer settings and play sequence\n"
               "  /R  print the current mixer settings and exit\n"
               "  /V  verbose: show BLASTER, mixer levels, DSP and IRQ details\n"
               "  /W  16-bit through WSS codec (OPL3-SAx and SB Pro compatibles)\n"
               "      AUTO detects it\n"
               "      OFF skips it\n"
               "      /W:530,7,3 sets values manually\n");
        return EXIT_USAGE;
    }

    /* The mixer report is all /R is for */
    if (report_only)
        verbose = 1;

    /* Timeouts for the DSP, the mixer setup already talks to it */
    pit_init();

    if (!env_get_blaster(&cfg))
        printf(" BLASTER: not set, PCM and the mixer need it\n");
    else if (verbose)
        print_env(&cfg);

    if (windows_running()) {
        printf("Running under Windows is not supported, exit Windows first\n");
        return EXIT_WINDOWS;
    }

    if (cfg.port != -1) {
        dsp_version_t ver;
        uint16_t base = (uint16_t)cfg.port;

        if (!dsp_reset(base)) {
            printf(" DSP: no answer at 0x%03X\n", base);
            wss_ignored = "no DSP";
        } else if (!dsp_get_version(base, &ver)) {
            printf(" DSP: reset OK, no version\n");
            wss_ignored = "no DSP version";
        } else {
            if (verbose)
                printf(" DSP: version %u.%02u%s\n", ver.major, ver.minor,
                       is_ess(base) ? ", ESS" : "");
            /* SB Pro compatibles, check for WSS codec next to the DSP */
            if (ver.major == 3 && !is_ess(base))
                sbpro_compatible = 1;
            else if (ver.major == 3)
                wss_ignored = "ESS plays 16-bit in its Extended mode";
            else if (ver.major >= 4)
                wss_ignored = "DSP 4.xx plays 16-bit itself";
            else
                wss_ignored = "this DSP plays mono only";
        }
    } else
        wss_ignored = "no BLASTER A";

    if (report_only) {
        if (wss_cfg.mode != WSS_UNSET)
            printf(" /W: ignored, /R only reports the mixer\n");
        mixer_report(&cfg);
        if (sbpro_compatible)
            sax_mixer_report();
        return EXIT_OK;
    }

    if (wss_ignored && wss_cfg.mode != WSS_UNSET)
        printf(" /W: ignored, %s\n", wss_ignored);

    if (sbpro_compatible)
        sax_detect();

    /* WSS codec and no /W  */
    if (sax_needs_config())
        return EXIT_WSS;

    if (set_mixer) {
        int mixer_saved = mixer_set_standard(&cfg);
        int sax_saved   = sbpro_compatible ? sax_mixer_set() : -1;

        /* Both, or whichever this card has */
        if (mixer_saved == -1)
            mixer_saved = sax_saved;
        else if (sax_saved != -1)
            mixer_saved &= sax_saved;

        if(mixer_saved != -1) {
            if(mixer_saved) {
                if (verbose)
                    printf(" Mixer: levels changed for capture, will restore at exit\n");
            } else
                printf(" Mixer: settings not accepted, set your mixer manually\n");
            atexit(mixer_restore);
        }
    }

    mixer = mixer_report(&cfg);
    /* The OPL3-SAx  */
    if (sbpro_compatible) {
        int sax_mixer = sax_mixer_report();

        if (sax_mixer != MIXER_NONE && sax_mixer != MIXER_OK && mixer != MIXER_UNKNOWN)
            mixer = sax_mixer;
    }
    if(mixer == MIXER_WARN && !set_mixer)
        printf("  NOTE: kept by /K, run without it to set the mixer for capture\n");

    /* Recorded in the watermark tone. No mixer chip counts as valid */
    mixer_ok = (mixer == MIXER_OK || mixer == MIXER_NONE);
    if(!mixer_ok)
        printf(" Mixer: not verified, watermarking\n");

    if (!vsync_calibrate(&hz)) {
        printf("No usable vertical retrace (VGA required)\n");
        return EXIT_NO_VGA;
    }

    pcm_init(&cfg, hz);
    if (pcm_irq_mismatch())
        return EXIT_BAD_IRQ;

    printf(" FM: Probing (%s)... ", opl_driver.name);
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
