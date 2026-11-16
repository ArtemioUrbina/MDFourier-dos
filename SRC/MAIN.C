/*
 *  MDFourier for DOS
 *  Released under the GNU GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <i86.h>
#include "carddrv.h"
#include "dsp.h"
#include "pcm.h"
#include "env.h"
#include "mixer.h"
#include "opl.h"
#include "pit.h"
#include "vsync.h"
#include "mdf.h"
#include "seq.h"
#include "kbd.h"
#include "host.h"
#include "cms.h"
#include "sb2seq.h"
#include "caps.h"
#include "midi.h"
#include "midiseq.h"
#include "midifam.h"
#include "midiset.h"
#include "profile.h"

/* Exit codes for batch files (IF ERRORLEVEL n is n or higher) */
#define EXIT_OK         0   /* sequence complete, capture usable */
#define EXIT_USAGE      1   /* bad command line */
#define EXIT_WINDOWS    2   /* running under Windows NT, 2000 or XP */
#define EXIT_NO_VGA     3   /* no OPL at 388h, or no CMS with /C */
#define EXIT_NO_FM      4   /* no OPL2/OPL3 at 0x388, CMS sequence: no CMS either */
#define EXIT_ABORTED    5   /* ESC pressed */
#define EXIT_BAD_FRAMES 6   /* frame out of tolerance, discard */
#define EXIT_WSS        7   /* DSP on another IRQ than BLASTER I */
#define EXIT_BAD_IRQ    8   /* the DSP raises another IRQ than BLASTER I */
#define EXIT_NO_MIDI    9   /* /M: no MPU-401 answered */

int timing_test(double hz);
void main_probe_fm(caps_t *caps);
void main_nothing_to_play(caps_t *caps, blaster_cfg_t *cfg, int cms_mode);
int main_midi_option(char *arg);
int main_midi(blaster_cfg_t *cfg);

int main(int argc, char *argv[]) {
    blaster_cfg_t cfg;
    caps_t        caps = { CAPS_FM_NONE, CMS_NONE, 0, 0, { 0, 0 }, 0, 0, 0, 0, MIXER_UNKNOWN, 0 };
    double        hz;
    int           i, set_mixer = 1, report_only = 0, result;
    int           bad_option = 0;
    char          *wss_ignored = NULL;
    int           timing_only = 0;
    int           cms_mode = 0;
    int           game_blaster;       /* CMS at BLASTER A, no DSP nor mixer */
    int           midi_mode = 0;

    printf("MDFourier DOS 0.96 [240p Test Suite Fourier Audio compare tool]\n");
    printf("Artemio Urbina 2026 free software under GPL http://junkerhq.net/MDFourier\n");

    /* Ctrl-C or Ctrl-Break would end us without the exit handlers, IRQ
     * vector and mixer left ours. Ignored, ESC stops MDF */
    signal(SIGINT, SIG_IGN);
    signal(SIGBREAK, SIG_IGN);

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
        } else if ((argv[i][0] == '/' || argv[i][0] == '-') &&
                   (argv[i][1] == 'M' || argv[i][1] == 'm')) {
            if (!main_midi_option(argv[i] + 2))
                bad_option = 1;
            midi_mode = 1;
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

    /* /M plays on its own, nothing else applies */
    if (midi_mode && (cms_mode || report_only || timing_only))
        bad_option = 1;

    if (bad_option) {
        printf("Usage: MDF [/C[:port]] [/K] [/M[:family][,set][,port]] [/R] [/T] [/V]\n"
               "           [/W:AUTO | /W:OFF | /W:port,irq,dma]\n"
               "  Needs a 386DX-33 with cache or faster\n"
               "  The mixer levels are set for playback and restored at exit\n"
               "  /C  CMS sequence: CMS, FM, 8-bit PCM and ADPCM, SB 2.0 and Game Blaster,\n"
               "      CMS at BLASTER A or /C:220\n"
               "  /K  keep the current mixer settings and play sequence\n"
               "  /M  MIDI through the MPU-401 at BLASTER P or 330, record the synth's\n"
               "      output. Family GM (default), GS, XG or MT32, set GM or MT32:\n"
               "      /M:GS, /M:GS,MT32 for a GS synth's MT-32 map, /M:MT32,300\n"
               "  /MT  the same as /M:MT32, for an MT-32, CM-32L or LAPC-I\n"
               "  /R  print the current mixer settings and exit\n"
               "  /T  frame timing test, 30s without sound, and exit\n"
               "  /V  verbose: BLASTER, mixer, IRQ, codec levels and SB/WSS context\n"
               "  /W  16-bit through WSS codec (OPL3-SAx and SB Pro compatibles),\n"
               "      a Crystal CS4236/CS4237B codec plays it without /W\n"
               "      AUTO detects it\n"
               "      OFF skips it\n"
               "      /W:530,7,3 sets values manually\n");
        return EXIT_USAGE;
    }

    if (report_only)
        verbose = 1;

    /* Under Windows the PIT doesn't keep time, try the TSC */
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

    /* /M only needs BLASTER for P */
    if (!env_get_blaster(&cfg)) {
        if (!midi_mode)
            printf(" BLASTER: not set%s\n", report_only ? "" : ", PCM plays fillers");
    } else if (verbose)
        env_print(&cfg);

    /* NT has no hardware access. A Windows DOS box runs with looser
     * timing, SB paths only */
    if (host_detect() == HOST_NT) {
        printf("Running under %s is not supported, it has no access to the\n"
               "hardware. Use DOS, or Windows 9x in MS-DOS mode\n", host_name());
        return EXIT_WINDOWS;
    }
    /* What the PIF should have, in each version's terms */
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

    if (midi_mode)
        return main_midi(&cfg);

    if (cms_mode && !report_only) {
        if (!caps.cms_port)
            caps.cms_port = cfg.port != -1 ? (unsigned)cfg.port : 0x220;
        caps.cms = cms_detect((uint16_t)caps.cms_port);
        if (caps.cms == CMS_NONE)
            printf(" CMS: not found at 0x%03X, %s\n", caps.cms_port, cms_detect_text());
        else
            printf(" CMS: 0x%03X, %s\n", caps.cms_port, cms_detect_text());
    }
    /* A Game Blaster at BLASTER A, no Sound Blaster to set up there */
    game_blaster = caps.cms == CMS_LATCH &&
                   (cfg.port == -1 || caps.cms_port == (unsigned)cfg.port);

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
            /* ESS ID only on a DSP 3.xx, as the AudioDrive reports */
            caps.dsp_ess = major == 3 && mixer_is_ess(base);
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

    /* 8-bit mono and ADPCM only, the WSS codec stays out of it */
    if (cms_mode)
        wss_ignored = "the CMS sequence plays no 16-bit";

    if (report_only) {
        if (env_wss_cfg.mode != WSS_UNSET)
            printf(" /W: ignored, /R only reports the mixer\n");
        if (cfg.port == -1)
            printf(" Mixer: nothing to report without BLASTER\n");
        if (caps.sbpro_compatible)
            cards_mixer_notes();
        mixer_report(&cfg);
        if (caps.sbpro_compatible) {
            cards_report((uint16_t)cfg.port);
            cards_mixer_report(MIXER_UNKNOWN);
        }
        return EXIT_OK;
    }

    /* FM gives the sync, or CMS with /C. Without either stop here */
    main_probe_fm(&caps);
    if (caps.fm == CAPS_FM_NONE && (!cms_mode || caps.cms == CMS_NONE)) {
        main_nothing_to_play(&caps, &cfg, cms_mode);
        return EXIT_NO_FM;
    }

    if (wss_ignored && env_wss_cfg.mode != WSS_UNSET)
        printf(" /W: ignored, %s\n", wss_ignored);

    /* The cards next to an SB Pro DSP: OPL3-SAx, Crystal (CARDS.C) */
    if (caps.sbpro_compatible && !cms_mode)
        cards_detect(&cfg);

    /* WSS codec and no /W  */
    if (cards_need_config())
        return EXIT_WSS;

    if (caps.sbpro_compatible)
        cards_mixer_notes();

    if (set_mixer && !game_blaster) {
        int mixer_saved = mixer_set_standard(&cfg);

        if (caps.sbpro_compatible)
            mixer_saved = cards_mixer_set(mixer_saved);

        if(mixer_saved != -1) {
            if (mixer_saved)
                mixer_how = MIXER_HOW_SET;
            else
                printf(" Mixer: settings not accepted, set your mixer manually\n");
            /* Exit handlers run last registered first */
            atexit(mixer_restore);
        }

        /* Effects that process the output: Crystal SRS 3D */
        if (caps.sbpro_compatible)
            cards_effects_off();
    }

    if (!set_mixer)
        mixer_how = MIXER_HOW_KEPT;

    if (game_blaster) {
        printf(" Mixer: none (Game Blaster), set the volume manually\n");
        caps.mixer = MIXER_NONE;
    } else
        caps.mixer = mixer_report(&cfg);

    /* The cards' own mixers, and an effect kept by /K */
    if (caps.sbpro_compatible) {
        caps.mixer = cards_mixer_report(caps.mixer);
        caps.mixer = cards_effects_check(caps.mixer);
    }

    /* A CS4237B maps the SB levels to its own, we show them */
    if (verbose && caps.sbpro_compatible)
        cards_report((uint16_t)cfg.port);

    /* What we do with the SB Pro filter for the _NF sweeps */
    if (verbose && !cms_mode) {
        if (mixer_sbpro_filter_switched())
            printf(" Mixer: output filter turned off for the _NF sweeps, back on after\n");
        else
            printf(" Mixer: output filter not switched for the _NF sweeps%s\n",
                   set_mixer ? ", no filter bit" : " (/K)");
    }

    if(caps.mixer == MIXER_WARN && !set_mixer)
        printf("  NOTE: kept by /K, run without it to set the mixer for capture\n");

    /* Recorded in the watermark tone. No mixer chip counts as valid */
    caps.mixer_ok = (caps.mixer == MIXER_OK || caps.mixer == MIXER_NONE);
    if(!caps.mixer_ok)
        printf(" Mixer: not verified%s, watermarking\n", cfg.port == -1 ? " (no BLASTER)" : "");

    if (!vsync_calibrate(&hz)) {
        printf("No usable vertical retrace (VGA required), measured %.2f Hz\n", hz);
        vsync_diagnose();
        return EXIT_NO_VGA;
    }

    /* No BLASTER: the PCM sections play fillers */
    if (!game_blaster && cfg.port != -1)
        caps.pcm = pcm_init(&cfg, hz, !cms_mode);
    if (pcm_irq_mismatch())
        return EXIT_BAD_IRQ;

    if (cms_mode) {
        /* Sync comes from FM, or from CMS on a Game Blaster */
        if (caps.pcm) {
            caps.adpcm = pcm_adpcm_probe();
            printf(" PCM: 8-bit mono, %s\n", caps.adpcm ? "ADPCM 4, 2.6 and 2-bit"
                                                         : "no ADPCM (the DSP ignored its commands)");
        }
        printf(" CMS sequence: FM %s, CMS %s, PCM %s, ADPCM %s, sync from %s\n",
               caps.fm != CAPS_FM_NONE ? "plays" : "filler",
               caps.cms != CMS_NONE ? "plays" : "filler",
               caps.pcm ? "plays" : "filler",
               caps.adpcm ? "plays" : "filler",
               caps.fm != CAPS_FM_NONE ? "FM" : "CMS");
        if (set_mixer && caps.sbpro_compatible)
            cards_before_play((uint16_t)cfg.port);
        PROFILE_PRINT(PROFILE_CMS_NAME, PROFILE_CMS_FILE);
        printf("Playing MDFourier CMS...\n");
        result = sb2seq_run(&caps);
    } else {
        if (set_mixer && caps.sbpro_compatible)
            cards_before_play((uint16_t)cfg.port);
        PROFILE_PRINT(PROFILE_MAIN_NAME, PROFILE_MAIN_FILE);
        printf("Playing MDFourier...\n");
        result = mdf_run_full_test(&caps);
    }

    /* silence the chip */
    if (caps.fm != CAPS_FM_NONE)
        opl_reset();

    if (result == SEQ_ABORTED)
        return EXIT_ABORTED;
    if (result == SEQ_BAD_FRAMES)
        return EXIT_BAD_FRAMES;
    return EXIT_OK;
}

/* OPL2/OPL3 at 0x388, set up for the sequence when found */
void main_probe_fm(caps_t *caps) {
    printf(" FM: Probing (OPL2/3 FM)... ");
    if (!opl_detect()) {
        printf("not found at 0x%03X\n", opl_port);
        return;
    }
    caps->fm = opl_is_opl3() ? CAPS_FM_OPL3 : CAPS_FM_OPL2;
    printf("found (%s)\n", caps->fm == CAPS_FM_OPL3 ? "OPL3" : "OPL2");
    opl_init();

    /* OPL3 and compatibles */
    if (caps->fm == CAPS_FM_OPL3)
        opl3_set_new_mode(1);
}

/* Nothing gives the sync: why and what to check. Usually a PnP card
 * not set up yet */
void main_nothing_to_play(caps_t *caps, blaster_cfg_t *cfg, int cms_mode) {
    printf("Nothing to play: no FM%s for the sync", cms_mode ? " or CMS" : "");
    if (cfg->port == -1)
        printf(", no BLASTER for PCM");
    else if (!caps->dsp)
        printf(", no DSP at 0x%03X", cfg->port);
    printf(".\n");

    if (!caps->dsp)
        printf("  Plug and Play cards need their setup utility before MDF.\n");
    if (cfg->port == -1)
        printf("  Then set BLASTER as the card is configured, e.g. SET BLASTER=A220 I5 D1 T4\n");
    else if (!caps->dsp)
        printf("  Check BLASTER A against the card's settings\n");
    else
        printf("  Check that the card's FM is enabled%s\n", cms_mode ? "" : ", or use /C with a CMS card");
}

/* /M: what the option chose */
midi_family_t *main_midi_family;
int            main_midi_set = -1;
unsigned       main_midi_port;

/* After /M: "", "T", ":GS", ":GS,MT32", ":MT32,300", "T:300". Names are
 * family then set, a number is the port */
int main_midi_option(char *arg) {
    char     copy[32], *token;
    int      names = 0;

    main_midi_family = midi_families[MIDIFAM_GM];
    if (*arg == 'T' || *arg == 't') {
        main_midi_family = midi_families[MIDIFAM_MT32];
        names = 1;
        arg++;
    }
    if (!*arg)
        return 1;
    if (*arg != ':' || strlen(arg) >= sizeof(copy))
        return 0;

    strcpy(copy, arg + 1);
    for (token = strtok(copy, ","); token; token = strtok(NULL, ",")) {
        if (*token >= '0' && *token <= '9') {
            if (sscanf(token, "%x", &main_midi_port) != 1 ||
                main_midi_port < 0x200 || main_midi_port > 0x3FE || (main_midi_port & 1))
                return 0;
        } else if (names == 0) {
            main_midi_family = midifam_find(token);
            if (!main_midi_family)
                return 0;
            names++;
        } else if (names == 1) {
            main_midi_set = midiset_find(token);
            if (main_midi_set < 0)
                return 0;
            names++;
        } else
            return 0;
    }
    return 1;
}

/* No SB setup, the synth is recorded from its own output */
int main_midi(blaster_cfg_t *cfg) {
    midi_family_t *family = main_midi_family;
    int      set = main_midi_set >= 0 ? main_midi_set : family->set;
    unsigned port = main_midi_port;
    int      from_blaster = !port && cfg->mpu_port != -1;
    double   hz;
    int      result;

    if (family->set_bank(set) == MIDIFAM_CANT) {
        printf(" MIDI: the %s family has no %s set\n", family->name, midi_sets[set].name);
        return EXIT_USAGE;
    }
    if (!port)
        port = cfg->mpu_port != -1 ? (unsigned)cfg->mpu_port : MIDI_PORT_DEFAULT;

    if (!midi_init((uint16_t)port)) {
        printf(" MIDI: no MPU-401 answered at 0x%03X. Check BLASTER P or give the port,\n"
               "  /M:%s,300, and that the card's MPU-401 is enabled\n", port, family->option);
        return EXIT_NO_MIDI;
    }
    /* Notes off and UART mode left, whatever way we exit */
    atexit(midi_exit);
    printf(" MIDI: MPU-401 at 0x%03X%s, UART mode%s\n", port,
           from_blaster ? " (BLASTER P)" : "",
           midi_uart_unconfirmed() ? " (not confirmed, sending anyway)" : "");

    if (!vsync_calibrate(&hz)) {
        printf("No usable vertical retrace (VGA required), measured %.2f Hz\n", hz);
        vsync_diagnose();
        return EXIT_NO_VGA;
    }

    printf(" MIDI: %s synth, %s set\n", family->name, midi_sets[set].name);
    PROFILE_PRINT(midi_sets[set].profile_name, midi_sets[set].profile);
    midiseq_setup(family, set);

    printf("Playing MDFourier MIDI...\n");
    result = midiseq_run();

    if (result == SEQ_ABORTED)
        return EXIT_ABORTED;
    if (result == SEQ_BAD_FRAMES)
        return EXIT_BAD_FRAMES;
    return EXIT_OK;
}

/* /T: 30 s of frames, no sound, to check the timing */
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
    /* DOS would have stopped at the first one */
    if (stats.bad && !host_is_windows())
        printf("  first bad frame: %lu\n", vsync_first_bad());
    return stats.bad ? EXIT_BAD_FRAMES : EXIT_OK;
}
