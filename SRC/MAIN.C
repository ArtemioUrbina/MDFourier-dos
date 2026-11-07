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
#include "mdf.h"
#include "seq.h"
#include "kbd.h"
#include "host.h"
#include "cms.h"
#include "cmsseq.h"
#include "caps.h"

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
#define EXIT_NO_CMS     9   /* /C: no CMS at the port */

int timing_test(double hz);

int main(int argc, char *argv[]) {
    blaster_cfg_t cfg;
    caps_t        caps = { CAPS_FM_NONE, CMS_NONE, 0, 0, { 0, 0 }, 0, 0, MIXER_UNKNOWN, 0 };
    double        hz;
    int           i, set_mixer = 1, report_only = 0, result;
    int           bad_option = 0;
    char          *wss_ignored = NULL;
    int           timing_only = 0;
    int           cms_mode = 0;
    int           game_blaster;       /* CMS without a DSP, nor a mixer */

    printf("MDFourier DOS 0.955 [240p Test Suite Fourier Audio compare tool]\n");
    printf("Artemio Urbina 2026 free software under GPL http://junkerhq.net/MDFourier\n");

    for (i = 1; i < argc; i++) {
        char opt = 0;

        if ((argv[i][0] == '/' || argv[i][0] == '-') && argv[i][1] && !argv[i][2])
            opt = argv[i][1];
        if ((argv[i][0] == '/' || argv[i][0] == '-') &&
            (argv[i][1] == 'W' || argv[i][1] == 'w') && argv[i][2] == ':') {
            if (!env_parse_wss(argv[i] + 3))
                bad_option = 1;
        } else if ((argv[i][0] == '/' || argv[i][0] == '-') &&
                   (argv[i][1] == 'C' || argv[i][1] == 'c') && argv[i][2] == ':') {
            if (sscanf(argv[i] + 3, "%x", &caps.cms_port) != 1 ||
                caps.cms_port < 0x210 || caps.cms_port > 0x260 || (caps.cms_port & 0x0F))
                bad_option = 1;
            cms_mode = 1;
        } else if (opt == 'C' || opt == 'c')
            cms_mode = 1;
        else if (opt == 'K' || opt == 'k')
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
        printf("Usage: MDF [/C[:port]] [/K] [/R] [/T] [/V] [/W:AUTO | /W:OFF | /W:port,irq,dma]\n"
               "  Needs a 486SX or faster with internal cache enabled\n"
               "  The mixer levels are set for playback and restored at exit\n"
               "  /C  CMS/Game Blaster sequence instead of FM and PCM, BLASTER A or /C:220\n"
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
                printf(" Timing: Windows' PIT, %.0f clocks per BIOS tick, %s\n"
                       "   (no CPU time stamp counter: %s)\n",
                       pit_ticks_measured(), pit_rate_mode(), pit_tsc_failure());
            else
                printf(" Timing: no CPU time stamp counter (%s),\n"
                       "   Windows' PIT may not time the frames\n", pit_tsc_failure());
        }
    } else
        pit_init();

    if (!env_get_blaster(&cfg))
        printf(" BLASTER: not set, PCM and the mixer need it\n");
    else if (verbose)
        env_print(&cfg);

    /* NT has no hardware access. A Windows 3.x/9x DOS box runs, with
     * looser timing, SB paths only and the Windows watermark */
    if (host_detect() == HOST_NT) {
        printf("Running under %s is not supported, it has no access to the\n"
               "hardware. Use DOS, or Windows 9x in MS-DOS mode\n", host_name());
        return EXIT_WINDOWS;
    }
    /* What the DOS box's PIF should have, in each version's own terms */
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

    if (cms_mode && !report_only) {
        if (!caps.cms_port)
            caps.cms_port = cfg.port != -1 ? (unsigned)cfg.port : 0x220;
        caps.cms = cms_detect((uint16_t)caps.cms_port);
        if (caps.cms == CMS_NONE) {
            printf(" CMS: not found at 0x%03X, %s\n", caps.cms_port, cms_detect_text());
            return EXIT_NO_CMS;
        }
        printf(" CMS: 0x%03X, %s\n", caps.cms_port, cms_detect_text());
    }
    game_blaster = caps.cms == CMS_LATCH;

    if (cfg.port != -1 && !game_blaster) {
        uint16_t base = (uint16_t)cfg.port;

        if (!dsp_reset(base)) {
            printf(" DSP: no answer at 0x%03X\n", base);
            wss_ignored = "no DSP";
        } else if (!dsp_get_version(base, &caps.dsp_version)) {
            printf(" DSP: reset OK, no version\n");
            wss_ignored = "no DSP version";
        } else {
            uint8_t major = caps.dsp_version.major;

            caps.dsp = 1;
            /* The ESS identify command goes only to a DSP 3.xx, the
             * version the AudioDrive reports */
            caps.dsp_ess = major == 3 && mixer_is_ess(base);
            if (verbose)
                printf(" DSP: version %u.%02u%s\n", major, caps.dsp_version.minor,
                       caps.dsp_ess ? ", ESS" : "");
            /* SB Pro compatibles, check for WSS codec next to the DSP */
            if (major == 3 && host_is_windows())
                wss_ignored = "Windows owns the card's 16-bit hardware";
            else if (major == 3 && !caps.dsp_ess)
                caps.sbpro_compatible = 1;
            else if (major == 3)
                wss_ignored = "ESS plays 16-bit in its Extended mode";
            else if (major >= 4)
                wss_ignored = "DSP 4.xx plays 16-bit itself";
            else
                wss_ignored = "this DSP plays mono only";
        }
    } else
        wss_ignored = "no BLASTER A";

    if (report_only) {
        if (env_wss_cfg.mode != WSS_UNSET)
            printf(" /W: ignored, /R only reports the mixer\n");
        mixer_report(&cfg);
        if (caps.sbpro_compatible)
            sax_mixer_report();
        return EXIT_OK;
    }

    if (wss_ignored && env_wss_cfg.mode != WSS_UNSET)
        printf(" /W: ignored, %s\n", wss_ignored);

    if (caps.sbpro_compatible)
        sax_detect();

    /* WSS codec and no /W  */
    if (sax_needs_config())
        return EXIT_WSS;

    if (set_mixer && !game_blaster) {
        int mixer_saved = mixer_set_standard(&cfg);
        int sax_saved   = caps.sbpro_compatible ? sax_mixer_set() : -1;

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

    if (game_blaster) {
        printf(" Mixer: none (Game Blaster), set the volume manually\n");
        caps.mixer = MIXER_NONE;
    } else
        caps.mixer = mixer_report(&cfg);

    /* The OPL3-SAx  */
    if (caps.sbpro_compatible) {
        int sax_mixer = sax_mixer_report();

        if (sax_mixer != MIXER_NONE && sax_mixer != MIXER_OK && caps.mixer != MIXER_UNKNOWN)
            caps.mixer = sax_mixer;
    }
    if(caps.mixer == MIXER_WARN && !set_mixer)
        printf("  NOTE: kept by /K, run without it to set the mixer for capture\n");

    /* Recorded in the watermark tone. No mixer chip counts as valid */
    caps.mixer_ok = (caps.mixer == MIXER_OK || caps.mixer == MIXER_NONE);
    if(!caps.mixer_ok)
        printf(" Mixer: not verified, watermarking\n");

    if (!vsync_calibrate(&hz)) {
        printf("No usable vertical retrace (VGA required), measured %.2f Hz\n", hz);
        vsync_diagnose();
        return EXIT_NO_VGA;
    }

    if (cms_mode) {
        printf("Playing MDFourier CMS...\n");
        result = cmsseq_run(&caps);
        if (result == SEQ_ABORTED)
            return EXIT_ABORTED;
        return result == SEQ_BAD_FRAMES ? EXIT_BAD_FRAMES : EXIT_OK;
    }

    pcm_init(&cfg, hz);
    if (pcm_irq_mismatch())
        return EXIT_BAD_IRQ;

    printf(" FM: Probing (%s)... ", opl_driver.name);
    if(opl_driver.detect()) {
        caps.fm = opl_is_opl3() ? CAPS_FM_OPL3 : CAPS_FM_OPL2;
        printf("found (%s)\n", caps.fm == CAPS_FM_OPL3 ? "OPL3" : "OPL2");
        opl_driver.init();

        /* OPL3 and compatibles */
        if (caps.fm == CAPS_FM_OPL3)
            opl3_set_new_mode(1);

        printf("Playing MDFourier...\n");
        result = mdf_run_full_test(&caps);

        /* silence the chip */
        opl_driver.reset();

    } else {
        printf("not found\n");
        return EXIT_NO_FM;
    }

    if (result == SEQ_ABORTED)
        return EXIT_ABORTED;
    if (result == SEQ_BAD_FRAMES)
        return EXIT_BAD_FRAMES;
    return EXIT_OK;
}

/* /T: 30 s of frames, no sound, to see how the timing holds. Under
 * Windows tolerant as the sequence would run */
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
