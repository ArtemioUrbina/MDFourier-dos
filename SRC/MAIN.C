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
#include "kbd.h"
#include "host.h"

#define SWEEP_FRAMES 20

/* Exit codes, for batch files (IF ERRORLEVEL n means n or higher) */
#define EXIT_OK         0   /* sequence complete, capture usable */
#define EXIT_USAGE      1   /* bad command line */
#define EXIT_WINDOWS    2   /* running under Windows NT, 2000 or XP */
#define EXIT_NO_VGA     3   /* no usable vertical retrace */
#define EXIT_NO_FM      4   /* no OPL2/OPL3 at 0x388 */
#define EXIT_ABORTED    5   /* ESC pressed */
#define EXIT_BAD_FRAMES 6   /* frame out of tolerance, discard */
#define EXIT_WSS        7   /* WSS codec found, /W needed */
#define EXIT_BAD_IRQ    8   /* the DSP raises another IRQ than BLASTER I */

int timing_test(double hz);

int main(int argc, char *argv[]) {
    blaster_cfg_t cfg;
    double        hz;
    int           stereo, i, set_mixer = 1, report_only = 0, mixer, mixer_ok, result;
    int           bad_option = 0;
    char          *wss_ignored = NULL;
    int           sbpro_compatible = 0;
    int           timing_only = 0;

    printf("MDFourier DOS 0.954 [240p Test Suite Fourier Audio compare tool]\n");
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
        else if (opt == 'T' || opt == 't')
            timing_only = 1;
        else
            bad_option = 1;
    }

    if (bad_option) {
        printf("Usage: MDF [/K] [/R] [/T] [/V] [/W:AUTO | /W:OFF | /W:port,irq,dma]\n"
               "  Needs a 486SX or faster with internal cache enabled\n"
               "  The mixer levels are set for playback and restored at exit\n"
               "  /K  keep the current mixer settings and play sequence\n"
               "  /R  print the current mixer settings and exit\n"
               "  /T  frame timing test, 30s without sound, and exit\n"
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

    /* Under Windows the PIT doesn't keep time, we try the CPU time stamp counter */
    if (host_is_windows()) {
        if (pit_use_tsc())
            printf(" Timing: CPU time stamp counter, %.2f MHz\n",
                   pit_tsc_hz() / 1000000.0);
        else {
            pit_init();
            if (pit_check_rate())
                printf(" Timing: Windows PIT, %.0f clocks per BIOS tick, %s\n"
                       "   (no CPU time stamp counter: %s)\n",
                       pit_ticks_measured(), pit_rate_mode(), pit_tsc_failure());
            else
                printf(" Timing: no CPU time stamp counter (%s),\n"
                       "   Windows PIT may not time the frames\n", pit_tsc_failure());
        }
    } else
        pit_init();

    if (!env_get_blaster(&cfg))
        printf(" BLASTER: not set, PCM and the mixer need it\n");
    else if (verbose)
        print_env(&cfg);

    /* NT has no hardware access.
     * Windows 3.x/9x DOS box has looser timing
     * We watermark both. This is just for curiosity or testing windows only cards */
    if (host_detect() == HOST_NT) {
        printf("Running under %s is not supported, it has no access to the\n"
               "hardware. Use DOS, or Windows 9x in MS-DOS mode\n", host_name());
        return EXIT_WINDOWS;
    }
    
    /* DOS box PIF for each version*/
    if (host_is_windows()) {
        printf(" %s DOS box: recorded with the Windows watermark,\n"
               "   ESS Extended mode and WSS codecs are left to Windows\n", host_name());
        if (host_windows_major() == 3)
            printf("   PIF: full screen, Exclusive, Detect Idle Time off, no Monitor Ports\n");
        else
            printf("   PIF: full screen, idle sensitivity Low, no screen saver or other programs\n");
        if (!host_windows_major())
            printf("   PIF: turn off \"Prevent MS-DOS-based programs from detecting Windows\"\n");
    }

    if (timing_only) {
        vsync_diagnose();
        if (!vsync_calibrate(&hz)) {
            printf("No usable vertical retrace (VGA required), measured %.2f Hz\n", hz);
            return EXIT_NO_VGA;
        }
        return timing_test(hz);
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
            if (ver.major == 3 && host_is_windows())
                wss_ignored = "Windows owns the card's 16-bit hardware";
            else if (ver.major == 3 && !is_ess(base))
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
        printf("No usable vertical retrace (VGA required), measured %.2f Hz\n", hz);
        vsync_diagnose();
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

#define TIMING_SECONDS  30

int timing_test(double hz) {
    vsync_stats_t stats;
    unsigned long frames = (unsigned long)(hz * TIMING_SECONDS), frame;

    printf(" Timing: %.4f Hz (%.4f ms per frame), %lu frames, ESC stops\n",
           hz, 1000.0 / hz, frames);
           
    kbd_poll_escape();
    vsync_set_tolerant(host_is_windows());
    vsync_begin();
    for (frame = 0; frame < frames; frame++) {
        if (kbd_poll_escape())
            break;
        vsync_wait();
    }
    vsync_end(&stats);
    vsync_print_stats(&stats, frame);
    
    /* The DOS sequence would have stopped at the first one */
    if (stats.bad && !host_is_windows())
        printf("  first bad frame: %lu\n", vsync_first_bad());
    return stats.bad ? EXIT_BAD_FRAMES : EXIT_OK;
}
