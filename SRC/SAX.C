/*
 * Yamaha OPL3-SAx (YMF711 OPL3-SA2, YMF715 OPL3-SA3) and similar
 *
 * A DSP 3.01 for Sound Blaster Pro and WSS codec at its own ports. 
 * 16-bit plays through the codec (WSS.C).
 *
 * Control registers (CTRL base, usually 370h) hold the master
 * volume for every source, the MIC, the 3D enhancement and the
 * hardware volume buttons.
 * 
 * Since they are not available through the SB Pro mixer, we set them
 * for playback and restored at exit like the others
 *
 * Source: Yamaha YMF715E (OPL3-SA3) data sheet https://theretroweb.com/chips/6084
 * 
 * SA3 functions were taken form teh data sheet and have not been tested yet
 */

#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include "sax.h"
#include "wss.h"
#include "env.h"
#include "mixer.h"

/* OPL3-SAx, at the codec's port + 0, read only: IRQ bits 5-3, DMA bits 2-0 */
#define SAX_CODEC_CONFIG 0

unsigned sax_codec_ports[] = { 0x530, 0xE80, 0xF40, 0x604 };
uint8_t  sax_config_irqs[8] = { 0, 7, 9, 10, 11, 0, 0, 0 };
int      sax_config_dmas[8] = { -1, 0, 1, 3, -1, -1, -1, -1 };
#define SAX_PORT_COUNT  (sizeof(sax_codec_ports) / sizeof(sax_codec_ports[0]))

unsigned sax_codec_port;
int      sax_needs_cfg;
char     sax_reason[64];

/* AUX2 is the OPL3-SAx FM input.
 * In SB mode the chip sets the FM level from the SB mixer
 * (FM 7 with master 7 is +4.5 dB) and I4/I5 aren't used
 *
 * After playing it stays in WSS mode and FM goes through I4/I5 as they are
 * (06h, +3 dB): measured 1.5 dB lower FM in the runs after a 16-bit one,
 * I4/I5 reading the same.
 * 
 * So whenever the codec is found, I4/I5 at +4.5 dB (SB mode level) and back at exit */
#define AUX2_SB_LEVEL   0x05    /* 1.5 dB steps, 08h = 0 dB: +4.5 dB */
#define AUX_LEVEL_BITS  0x1F

int     aux2_saved;
uint8_t aux2_saved_l, aux2_saved_r;

void sax_aux2_restore() {
    if (!aux2_saved)
        return;
    wss_out(WSS_AUX2_L, aux2_saved_l, 0);
    wss_out(WSS_AUX2_R, aux2_saved_r, 0);
    aux2_saved = 0;
}

/* For /V */
void sax_dump(char *when) {
    uint8_t reg;

    if (!verbose)
        return;
    
    printf(" PCM: WSS codec %s, I0-I7:", when);
    for (reg = 0; reg < 8; reg++)
        printf(" %02X", wss_in(reg)); 

    printf(", ID %02X\n", wss_in(WSS_MODE_ID));
}

void sax_codec_found(char *when) {
    wss_select(sax_codec_port);
    sax_dump(when);

    aux2_saved_l = wss_in(WSS_AUX2_L);
    aux2_saved_r = wss_in(WSS_AUX2_R);
    wss_out(WSS_AUX2_L, (uint8_t)((aux2_saved_l & ~AUX_LEVEL_BITS) | AUX2_SB_LEVEL), 0);
    wss_out(WSS_AUX2_R, (uint8_t)((aux2_saved_r & ~AUX_LEVEL_BITS) | AUX2_SB_LEVEL), 0);
    aux2_saved = 1;
    atexit(sax_aux2_restore);
}

/* Control registers: index at CTRL base, data at +1 */

#define SAX_POWER       0x01    /* bits 2-0 power save/down, 0 normal */
#define SAX_MASTER_L    0x07    /* bit 7 mute, bits 3-0 attenuation, 2 dB steps */
#define SAX_MASTER_R    0x08
#define SAX_MIC         0x09    /* bit 7 mute, bits 4-0 gain, +12 to -34.5 dB */
#define SAX_MISC        0x0A    /* bit 7 VEN, bit 3 MODE (1 WSS), bits 2-0 version */
#define SAX_ANALOG_PD   0x13    /* SA3: analog blocks powered down, 0 normal */
#define SAX_WIDE        0x14    /* SA3: bits 6-4 R, 2-0 L, 0-7 is 0-100% */
#define SAX_BASS        0x15    /* SA3: same layout, 1.5 dB steps */
#define SAX_TREBLE      0x16

#define SAX_MUTE        0x80
#define SAX_VEN         0x80    /* hardware volume buttons on */
#define SAX_MODE_WSS    0x08
#define SAX_VERSION     0x07
#define SAX_VER_SA2     1       /* YMF711, no 3D registers */

/* Any even port from 100h, these are the usual ones */
unsigned sax_ctrl_ports[] = { 0x370, 0x100, 0x310, 0x538 };
#define SAX_CTRL_COUNT  (sizeof(sax_ctrl_ports) / sizeof(sax_ctrl_ports[0]))

unsigned sax_ctrl_port;         /* 0 means not found */
uint8_t  sax_version;
int      sax_probed;

uint8_t ctrl_in(uint8_t reg) {
    outp(sax_ctrl_port, reg);
    return (uint8_t)inp(sax_ctrl_port + 1);
}

void ctrl_out(uint8_t reg, uint8_t val) {
    outp(sax_ctrl_port, reg);
    outp(sax_ctrl_port + 1, val);
}

/* The version bits read only, the master volume read/write.
 * Empty ports read FFh */
int sax_ctrl_present(unsigned port) {
    uint8_t misc, again, master, test;

    sax_ctrl_port = port;
    misc = ctrl_in(SAX_MISC);
    if (misc == 0xFF || !(misc & SAX_VERSION))
        return 0;

    ctrl_out(SAX_MISC, (uint8_t)(misc ^ SAX_VERSION));
    again = ctrl_in(SAX_MISC);
    ctrl_out(SAX_MISC, misc);
    if ((again & SAX_VERSION) != (misc & SAX_VERSION))
        return 0;

    master = ctrl_in(SAX_MASTER_L);
    test = (uint8_t)(master ^ 0x0F);
    ctrl_out(SAX_MASTER_L, test);
    again = ctrl_in(SAX_MASTER_L);
    ctrl_out(SAX_MASTER_L, master);
    if ((again & 0x8F) != (test & 0x8F))
        return 0;

    sax_version = (uint8_t)(misc & SAX_VERSION);
    return 1;
}

void sax_find_ctrl() {
    unsigned i;

    for (i = 0; i < SAX_CTRL_COUNT; i++)
        if (sax_ctrl_present(sax_ctrl_ports[i]))
            return;

    sax_ctrl_port = 0;
}

int sax_has_3d() {
    return sax_version != SAX_VER_SA2;
}

char *sax_name() {
    return sax_version == SAX_VER_SA2 ? "OPL3-SA2" : "OPL3-SA3";
}

/* For capture we set master 0 dB, MIC muted, the buttons off, 3D flat */
typedef struct {
    uint8_t reg, mask, value;
} sax_std_t;

sax_std_t sax_std[] = {
    { SAX_MASTER_L, 0x8F, 0x00 },       /* 0 dB, not muted */
    { SAX_MASTER_R, 0x8F, 0x00 },
    { SAX_MIC,      0x80, 0x80 },       /* muted */
    { SAX_MISC,     0x80, 0x00 },       /* VEN off */
    { SAX_WIDE,     0x77, 0x00 },       /* SA3 from here: 3D off */
    { SAX_BASS,     0x77, 0x00 },
    { SAX_TREBLE,   0x77, 0x00 }
};
#define SAX_STD_ALL     (sizeof(sax_std) / sizeof(sax_std[0]))
#define SAX_STD_SA2     4

uint8_t  sax_saved[SAX_STD_ALL];
unsigned sax_saved_count;

unsigned sax_std_count() {
    return sax_has_3d() ? SAX_STD_ALL : SAX_STD_SA2;
}

void sax_mixer_restore() {
    unsigned i;

    for (i = 0; i < sax_saved_count; i++)
        ctrl_out(sax_std[i].reg, sax_saved[i]);

    sax_saved_count = 0;
}

int sax_find_codec();

/* Codec and then control port next to it */
void sax_probe() {
    if (sax_probed)
        return;

    sax_probed = 1;
    if (wss_cfg.mode == WSS_MANUAL) {
        if (wss_present(wss_cfg.port))
            sax_codec_port = wss_cfg.port;
    }
    else
        sax_find_codec();

    if (sax_codec_port)
        sax_find_ctrl();
}

int sax_mixer_set() {
    unsigned i, count;
    int ok = 1;

    sax_probe();
    if (!sax_ctrl_port)
        return -1;

    count = sax_std_count();
    for (i = 0; i < count; i++) {
        sax_saved[i] = ctrl_in(sax_std[i].reg);
        ctrl_out(sax_std[i].reg, (uint8_t)((sax_saved[i] & ~sax_std[i].mask) | sax_std[i].value));
    }

    if (!sax_saved_count)
        atexit(sax_mixer_restore);
    sax_saved_count = count;

    for (i = 0; i < count; i++)
        if ((ctrl_in(sax_std[i].reg) & sax_std[i].mask) != sax_std[i].value)
            ok = 0;

    return ok;
}

/* -2 dB steps from 0 */
int sax_master_db(uint8_t val) {
    return -2 * (val & 0x0F);
}

/* +12 dB down in 1.5 dB steps, in tenths */
int sax_mic_tenths(uint8_t val) {
    return 120 - 15 * (val & 0x1F);
}

int sax_mixer_report() {
    uint8_t mstl, mstr, mic, misc, power, analog_pd, wide = 0, bass = 0, treble = 0;
    int warn, low, mic_on, buttons, d3, powered_down;

    sax_probe();
    if (!sax_ctrl_port) {
        if (!sax_codec_port)
            return MIXER_NONE;

        printf(" Mixer: OPL3-SAx control registers not found (370h, 100h, 310h, 538h),\n"
               "        check master, MIC and 3D with the card's own utility\n");
        return MIXER_UNKNOWN;
    }

    mstl      = ctrl_in(SAX_MASTER_L);
    mstr      = ctrl_in(SAX_MASTER_R);
    mic       = ctrl_in(SAX_MIC);
    misc      = ctrl_in(SAX_MISC);
    power     = (uint8_t)(ctrl_in(SAX_POWER) & 0x07);
    /* 13h and up are SA3 registers, an SA2 reads something else there */
    analog_pd = 0;
    if (sax_has_3d()) {
        analog_pd = (uint8_t)(ctrl_in(SAX_ANALOG_PD) & 0x1F);
        wide   = (uint8_t)(ctrl_in(SAX_WIDE) & 0x77);
        bass   = (uint8_t)(ctrl_in(SAX_BASS) & 0x77);
        treble = (uint8_t)(ctrl_in(SAX_TREBLE) & 0x77);
    }

    low          = (mstl & 0x8F) || (mstr & 0x8F);
    mic_on       = !(mic & SAX_MUTE);
    buttons      = (misc & SAX_VEN) != 0;
    d3           = wide || bass || treble;
    powered_down = power || analog_pd;
    warn         = low || mic_on || buttons || d3 || powered_down;

    if (verbose || warn) {
        printf(" Mixer: %s at 0x%03X, master %d/%d dB%s, MIC %s, %s mode\n",
               sax_name(), sax_ctrl_port, sax_master_db(mstl), sax_master_db(mstr),
               ((mstl | mstr) & SAX_MUTE) ? " muted" : "",
               mic_on ? "on" : "muted", (misc & SAX_MODE_WSS) ? "WSS" : "SB");

        if (sax_has_3d())
            printf("        3D wide %u/%u (7 = 100%%), bass %u/%u, treble %u/%u (1.5 dB steps)\n",
                   wide & 7, wide >> 4, bass & 7, bass >> 4, treble & 7, treble >> 4);

        printf("        hardware volume buttons %s\n", buttons ? "on" : "off");
    }

    if (low)
        printf("  WARNING: master below 0 dB or muted\n");
    if (mic_on)
        printf("  WARNING: MIC not muted (%d.%d dB), adds noise\n",
               sax_mic_tenths(mic) / 10, abs(sax_mic_tenths(mic) % 10));
    if (d3)
        printf("  WARNING: 3D, bass or treble on, alters the frequency response\n");
    if (buttons)
        printf("  WARNING: hardware volume buttons on, could change the level mid-run\n");
    if (powered_down)
        printf("  WARNING: blocks powered down (01h %02Xh, 13h %02Xh), may be silent\n",
               power, analog_pd);

    if (!warn) {
        printf(" Mixer: %s, OK for capture\n", sax_name());
        return MIXER_OK;
    }

    return MIXER_WARN;
}

int sax_find_codec() {
    unsigned i;

    for (i = 0; i < SAX_PORT_COUNT; i++)
        if (wss_present(sax_codec_ports[i])) {
            sax_codec_port = sax_codec_ports[i];
            return 1;
        }

    return 0;
}

/* DMA from the configuration register, the IRQ from it, or probed when
 * "probe" is set. IRQ 0: left to probe later. */
void sax_codec_settings(int *irq, int *dma, int probe) {
    uint8_t config = (uint8_t)inp(sax_codec_port + SAX_CODEC_CONFIG);

    *dma = sax_config_dmas[config & 7];
    *irq = -1;
    if (*dma < 0)
        return;

    *irq = sax_config_irqs[(config >> 3) & 7];
    if (!*irq && probe) {
        *irq = wss_probe_irq((uint8_t)*dma);
        if (!*irq)
            *irq = -1;
    }
}

/* Without /W: what was found and the options, then the run stops */
void sax_print_options(int irq, int dma) {
    char given[24];

    printf(" PCM: WSS codec found at 0x%03X", sax_codec_port);
    if (irq > 0)
        printf(", IRQ %d", irq);
    if (dma >= 0)
        printf(", DMA %d", dma);
    printf("\n"
           "      MDF can play 16-bit through it, but doesn't know its card's mixer\n"
           "      set its wave and master levels with the card's utility,\n"
           "      then run MDF with one of:\n");

    /* One column for the options, whatever the port and IRQ widths */
    if (irq > 0 && dma >= 0)
        sprintf(given, "/W:%X,%d,%d", sax_codec_port, irq, dma);
    else
        sprintf(given, "/W:%X,irq,dma", sax_codec_port);
    printf("        %-15s %s\n", given,
           irq > 0 && dma >= 0 ? "these settings" : "its settings, from the card's setup");
    printf("        %-15s detect them each run\n"
           "        %-15s without 16-bit\n", "/W:AUTO", "/W:OFF");
}

void sax_detect() {
    int irq = -1, dma = -1;

    sax_probe();
    if (wss_cfg.mode == WSS_MANUAL) {
        if (!sax_codec_port) {
            sprintf(sax_reason, "no WSS codec at 0x%03X, check /W", wss_cfg.port);
            return;
        }
        sax_codec_found("at start");
        wss_use(sax_codec_port, (uint8_t)wss_cfg.irq, (uint8_t)wss_cfg.dma);
        return;
    }

    if (!sax_codec_port) {
        if (wss_cfg.mode == WSS_AUTO)
            sprintf(sax_reason, "no WSS codec found for /W:AUTO");
        return;
    }

    if (wss_cfg.mode == WSS_OFF) {
        sax_codec_found("not used (/W:OFF)");
        sprintf(sax_reason, "WSS codec at 0x%03X left out by /W:OFF", sax_codec_port);
        return;
    }

    sax_codec_found("at start");

    /* A card whose mixer MDF sets needs no /W: as /W:AUTO */
    if (wss_cfg.mode == WSS_UNSET && sax_ctrl_port) {
        wss_cfg.mode = WSS_AUTO;
        if (verbose)
            printf(" PCM: WSS codec at 0x%03X, %s mixer known: /W:AUTO\n",
                   sax_codec_port, sax_name());
    }

    /* Without /W the run stops here */
    sax_codec_settings(&irq, &dma, wss_cfg.mode == WSS_UNSET);

    if (wss_cfg.mode == WSS_UNSET) {
        sax_print_options(irq, dma);
        sax_needs_cfg = 1;
        return;
    }

    if (irq < 0 || dma < 0) {
        sprintf(sax_reason, "WSS IRQ or DMA not found, give them with /W:%X,irq,dma", sax_codec_port);
        return;
    }
    /* IRQ 0: probed before the 16-bit section, FM blocks are over */
    wss_use(sax_codec_port, (uint8_t)irq, (uint8_t)dma);
}

int sax_needs_config() {
    return sax_needs_cfg;
}

char *sax_no_16bit_reason() {
    return sax_reason[0] ? sax_reason : NULL;
}
