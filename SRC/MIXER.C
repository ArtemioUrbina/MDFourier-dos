#include <stdio.h>
#include <conio.h>
#include "mixer.h"

/* Sources:
 * https://pdos.csail.mit.edu/6.828/2017/readings/hardware/SoundBlaster.pdf
 * https://www.philscomputerlab.com/uploads/3/7/2/3/37231621/es1869techmanual.pdf
 */

/* Mixer index/data ports, relative to the BLASTER port */
#define MIX_INDEX   4
#define MIX_DATA    5

/* SB Pro (CT1345) registers: left in bits 7-5, right in 3-1,
 * 0-7 = -46 dB to 0 dB. Not mute, 0 is -46 dB */
#define SBP_VOICE   0x04
#define SBP_MIC     0x0A        /* bits 2-1 */
#define SBP_MASTER  0x22
#define SBP_FM      0x26
#define SBP_CD      0x28
#define SBP_LINE    0x2E

/* SB16 (CT1745) registers: volumes in bits 7-3, 31 = 0 dB, 2 dB steps */
#define SB16_MASTER 0x30        /* L, R at +1 */
#define SB16_VOICE  0x32
#define SB16_FM     0x34
#define SB16_CD     0x36
#define SB16_LINE   0x38
#define SB16_MIC    0x3A
#define SB16_PCSPK  0x3B        /* bits 7-6, 0-3 = -18 dB to 0 dB, no mute */
#define SB16_OUTSW  0x3C        /* bit0 mic, 1-2 CD R/L, 3-4 line R/L */
#define SB16_GAIN   0x41        /* L, R at +1: bits 7-6, x1 x2 x4 x8 */
#define SB16_TREBLE 0x44        /* L, R at +1: bits 7-4, 7 and 8 = 0 dB */
#define SB16_BASS   0x46

/* ESS ES18xx extended registers (ES1869 data sheet, Tables 18-24).
 * Volumes are L/R nibbles: FM and mic 8 = 0 dB, 15 = +10.5 dB;
 * CD, aux, line, mono in 13 = 0 dB; 0 = mute */
#define ESS_ID      0x40        /* reads 0x18, then 0x69 */
#define ESS_MIC     0x1A
#define ESS_FM      0x36        /* music DAC */
#define ESS_CD      0x38        /* AuxA */
#define ESS_AUX     0x3A        /* AuxB */
#define ESS_PCSPK   0x3C        /* bits 2-0 */
#define ESS_LINE    0x3E
#define ESS_3D      0x50        /* bit 3: 3-D effect on */
#define ESS_MASTER  0x60        /* L, R at 0x62: bits 5-0, 63 = max, bit 6 mute */
#define ESS_MONOIN  0x6D
#define ESS_MONOMIX 0x7D        /* bit 0: mono in mixed after master */

typedef struct {
    uint8_t reg;
    uint8_t mask;
    uint8_t value;
    uint8_t check;
} mix_std_t;

static const mix_std_t sb16_std[] = {
    { SB16_MASTER,     0xF8, 0xF8, 0xF8 },  /* 0 dB */
    { SB16_MASTER + 1, 0xF8, 0xF8, 0xF8 },
    { SB16_FM,         0xF8, 0xF8, 0xF8 },
    { SB16_FM + 1,     0xF8, 0xF8, 0xF8 },
    { SB16_GAIN,       0xC0, 0x00, 0xC0 },  /* x1 */
    { SB16_GAIN + 1,   0xC0, 0x00, 0xC0 },
    { SB16_TREBLE,     0xF0, 0x80, 0xF0 },  /* flat */
    { SB16_TREBLE + 1, 0xF0, 0x80, 0xF0 },
    { SB16_BASS,       0xF0, 0x80, 0xF0 },
    { SB16_BASS + 1,   0xF0, 0x80, 0xF0 },
    { SB16_OUTSW,      0x1F, 0x00, 0x1F },  /* no CD, line, mic */
    { SB16_PCSPK,      0xC0, 0x00, 0xC0 }   /* minimum, -18 dB */
};

/* Match on ESS, the low bits are ignored on a real SB Pro */
static const mix_std_t sbpro_std[] = {
    { SBP_MASTER,      0xFF, 0xFF, 0xEE },  /* max */
    { SBP_FM,          0xFF, 0xFF, 0xEE },
    { SBP_CD,          0xFF, 0x00, 0xEE },  /* muted */
    { SBP_LINE,        0xFF, 0x00, 0xEE },
    { SBP_MIC,         0x06, 0x00, 0x06 }
};

/* In ESS set the extended registers, after the SB Pro view */
static const mix_std_t ess_std[] = {
    { ESS_MASTER,      0x7F, 0x3F, 0x7F },  /* max, not muted */
    { ESS_MASTER + 2,  0x7F, 0x3F, 0x7F },
    { ESS_FM,          0xFF, 0x88, 0xFF },  /* 0 dB, 15 is +10.5 dB */
    { ESS_CD,          0xFF, 0x00, 0xFF },  /* muted */
    { ESS_LINE,        0xFF, 0x00, 0xFF },
    { ESS_MIC,         0xFF, 0x00, 0xFF },
    { ESS_AUX,         0xFF, 0x00, 0xFF },
    { ESS_MONOIN,      0xFF, 0x00, 0xFF },
    { ESS_PCSPK,       0x07, 0x00, 0x07 },
    { ESS_MONOMIX,     0x01, 0x00, 0x01 },  /* no mono in after master */
    { ESS_3D,          0x08, 0x00, 0x08 }   /* 3-D effect off */
};

/* Previous state, for mixer_restore(), in write order */
#define MIX_SAVE_MAX 24

static uint16_t saved_base;
static unsigned saved_count;
static uint8_t  saved_reg[MIX_SAVE_MAX];
static uint8_t  saved_val[MIX_SAVE_MAX];

uint8_t mix_read(uint16_t base, uint8_t reg) {
    outp(base + MIX_INDEX, reg);
    return (uint8_t)inp(base + MIX_DATA);
}

void mix_write(uint16_t base, uint8_t reg, uint8_t val) {
    outp(base + MIX_INDEX, reg);
    outp(base + MIX_DATA, val);
}

/* Mixer answers: flip the voice volume bits, read back, restore */
int mix_present(uint16_t base, uint8_t reg, uint8_t mask) {
    uint8_t old, test, got;

    old  = mix_read(base, reg);
    test = (uint8_t)(old ^ mask);
    mix_write(base, reg, test);
    got  = mix_read(base, reg);
    mix_write(base, reg, old);

    return ((got ^ test) & mask) == 0;
}

/* ES18xx identification */
int is_ess(uint16_t base) {
    return mix_read(base, ESS_ID) == 0x18;
}

int sb16_db(uint8_t val) {
    return 2 * ((val >> 3) - 31);
}

int report_sb16(uint16_t base) {
    uint8_t mstl, mstr, fml, fmr, outsw, gl, gr, tl, tr, bl, br, mic, pcspk;
    int warn = 0;

    mstl  = mix_read(base, SB16_MASTER);
    mstr  = mix_read(base, SB16_MASTER + 1);
    fml   = mix_read(base, SB16_FM);
    fmr   = mix_read(base, SB16_FM + 1);
    outsw = mix_read(base, SB16_OUTSW);
    gl    = (uint8_t)(mix_read(base, SB16_GAIN) >> 6);
    gr    = (uint8_t)(mix_read(base, SB16_GAIN + 1) >> 6);
    tl    = (uint8_t)(mix_read(base, SB16_TREBLE) >> 4);
    tr    = (uint8_t)(mix_read(base, SB16_TREBLE + 1) >> 4);
    bl    = (uint8_t)(mix_read(base, SB16_BASS) >> 4);
    br    = (uint8_t)(mix_read(base, SB16_BASS + 1) >> 4);
    mic   = (uint8_t)(mix_read(base, SB16_MIC) >> 3);
    pcspk = (uint8_t)(mix_read(base, SB16_PCSPK) >> 6);

    printf("Mixer: SB16, master %d/%d dB, FM %d/%d dB, gain x%d/x%d\n",
           sb16_db(mstl), sb16_db(mstr), sb16_db(fml), sb16_db(fmr),
           1 << gl, 1 << gr);
    printf("       treble %u/%u, bass %u/%u (7 or 8 = flat), PC speaker %d dB\n",
           tl, tr, bl, br, 6 * pcspk - 18);
    printf("       to output:%s%s%s%s\n",
           (outsw & 0x06) ? " CD" : "", (outsw & 0x18) ? " line" : "",
           (outsw & 0x01) ? " mic" : "", (outsw & 0x1F) ? "" : " none");

    if ((mstl >> 3) != 31 || (mstr >> 3) != 31 || (fml >> 3) != 31 || (fmr >> 3) != 31) {
        printf("  WARNING: master or FM below 0 dB\n");
        warn = 1;
    }
    if (gl || gr) {
        printf("  WARNING: output gain boosted, may clip\n");
        warn = 1;
    }
    /* All zero: real SB16 at minimum, or DOSBox, which starts them at
     * 0 and doesn't emulate tone controls. Can't tell which */
    if (!tl && !tr && !bl && !br) {
        printf("  NOTE: treble/bass read 0. minimum on a SB16 or DOSBox\n");
        warn = 1;
    } else if (tl < 7 || tl > 8 || tr < 7 || tr > 8 ||
               bl < 7 || bl > 8 || br < 7 || br > 8) {
        printf("  WARNING: treble/bass not flat, alters the frequency response\n");
        warn = 1;
    }
    /* CD, line, mic with any volume */
    if ((outsw & 0x1E) || ((outsw & 0x01) && mic)) {
        printf("  WARNING: other inputs mixed into the output, adds noise\n");
        warn = 1;
    }
    /* Can't be muted, set to minimum */
    if (pcspk) {
        printf("  WARNING: PC speaker above minimum, may add noise\n");
        warn = 1;
    }
    if (!warn) {
        printf("Mixer: OK for capture\n");
        return 1;
    }
    return 0;
}

/* Only tested in DosBox */
int report_sbpro(uint16_t base) {
    uint8_t mst, fm, cd, line, mic;
    int warn = 0;

    mst  = mix_read(base, SBP_MASTER);
    fm   = mix_read(base, SBP_FM);
    cd   = mix_read(base, SBP_CD);
    line = mix_read(base, SBP_LINE);
    mic  = (uint8_t)((mix_read(base, SBP_MIC) >> 1) & 0x03);

    printf("Mixer: SB Pro, master %u/%u, FM %u/%u (7 = 0 dB)\n",
           mst >> 5, (mst >> 1) & 7, fm >> 5, (fm >> 1) & 7);
    printf("       CD %u/%u, line %u/%u (0 = -46 dB), mic %u/3\n",
           cd >> 5, (cd >> 1) & 7, line >> 5, (line >> 1) & 7, mic);

    /* bits 4 and 0 are unused, 0xEE is max */
    if ((mst & 0xEE) != 0xEE || (fm & 0xEE) != 0xEE) {
        printf("  WARNING: master or FM below max\n");
        warn = 1;
    }
    if ((cd & 0xEE) || (line & 0xEE) || mic) {
        printf("  WARNING: CD, line or mic not muted, adds noise\n");
        warn = 1;
    }
    if (!warn) {
        printf("Mixer: OK for capture\n");
        return 1;
    }
    return 0;
}

/* ESS extended registers, the SB Pro view hides mutes */
int report_ess(uint16_t base) {
    uint8_t mstl, mstr, fm, cd, line, mic, aux, mono, pcspk, mmix, d3;
    int warn = 0;

    mstl  = mix_read(base, ESS_MASTER);
    mstr  = mix_read(base, ESS_MASTER + 2);
    fm    = mix_read(base, ESS_FM);
    cd    = mix_read(base, ESS_CD);
    line  = mix_read(base, ESS_LINE);
    mic   = mix_read(base, ESS_MIC);
    aux   = mix_read(base, ESS_AUX);
    mono  = mix_read(base, ESS_MONOIN);
    pcspk = (uint8_t)(mix_read(base, ESS_PCSPK) & 0x07);
    mmix  = (uint8_t)(mix_read(base, ESS_MONOMIX) & 0x01);
    d3    = (uint8_t)(mix_read(base, ESS_3D) & 0x08);

    printf("Mixer: ESS, master %u/%u%s (63 = max), FM %u/%u (8 = 0 dB)%s\n",
           mstl & 0x3F, mstr & 0x3F, ((mstl | mstr) & 0x40) ? " muted" : "",
           fm >> 4, fm & 0x0F, d3 ? ", 3-D on" : "");
    printf("       CD %u/%u, line %u/%u, mic %u/%u, aux %u/%u, mono in %u/%u, PC speaker %u\n",
           cd >> 4, cd & 0x0F, line >> 4, line & 0x0F, mic >> 4, mic & 0x0F,
           aux >> 4, aux & 0x0F, mono >> 4, mono & 0x0F, pcspk);

    if ((mstl & 0x7F) != 0x3F || (mstr & 0x7F) != 0x3F) {
        printf("  WARNING: master below max or muted\n");
        warn = 1;
    }
    if (fm != 0x88) {
        printf("  WARNING: FM not at 0 dB, %s\n",
               ((fm >> 4) > 8 || (fm & 0x0F) > 8) ? "boosted, may clip" : "below 0 dB");
        warn = 1;
    }
    if (d3) {
        printf("  WARNING: 3-D effect on, alters the output\n");
        warn = 1;
    }
    if (cd || line || mic || aux || mono || pcspk || mmix) {
        printf("  WARNING: other inputs not muted, adds noise\n");
        warn = 1;
    }
    if (!warn) {
        printf("Mixer: OK for capture\n");
        return 1;
    }
    return 0;
}

int mixer_report(blaster_cfg_t *cfg) {
    uint16_t base;

    if (cfg->port == -1 || cfg->type == -1) {
        printf("Mixer: check volumes manually\n");
        return -1;
    }
    base = (uint16_t)cfg->port;

    switch (cfg->type) {
    case 1:
    case 3:
        printf("Mixer: none (SB 1.x/2.0), set the volume knob manually\n");
        return -1;
    case 2:
    case 4:
    case 5:
        /* ESS18 via ID. The voice test would leave it a step lower */
        if (is_ess(base))
            return report_ess(base);
        if (mix_present(base, SBP_VOICE, 0xEE))
            return report_sbpro(base);
        printf("Mixer: SB Pro expected, not answering\n");
        return -1;
    case 6:
        if (mix_present(base, SB16_VOICE, 0xF8))
            return(report_sb16(base));
        printf("Mixer: SB16 expected, not answering\n");
        return -1;
    default:
        printf("Mixer: unknown type T%d, check volumes manually\n", cfg->type);
    }
    return -1;
}

void mix_save(uint16_t base, const mix_std_t *std, unsigned count) {
    unsigned i;

    saved_base = base;
    for (i = 0; i < count && saved_count < MIX_SAVE_MAX; i++) {
        saved_reg[saved_count] = std[i].reg;
        saved_val[saved_count] = mix_read(base, std[i].reg);
        saved_count++;
    }
}

int mix_set(uint16_t base, const mix_std_t *std, unsigned count) {
    unsigned i;
    int ok = 1;

    for (i = 0; i < count; i++)
        mix_write(base, std[i].reg,
                  (uint8_t)((mix_read(base, std[i].reg) & ~std[i].mask) | std[i].value));
    for (i = 0; i < count; i++) {
        if ((mix_read(base, std[i].reg) & std[i].check) != (std[i].value & std[i].check))
            ok = 0;
    }
    return ok;
}

#define COUNT(t) (sizeof(t) / sizeof(t[0]))

int mixer_set_standard(blaster_cfg_t *cfg) {
    uint16_t base;
    int ok;

    if (cfg->port == -1 || cfg->type == -1)
        return -1;
    base = (uint16_t)cfg->port;

    switch (cfg->type) {
    case 2:
    case 4:
    case 5:
        if (is_ess(base)) {
            /* Extended registers last */
            mix_save(base, sbpro_std, COUNT(sbpro_std));
            mix_save(base, ess_std, COUNT(ess_std));
            ok = mix_set(base, sbpro_std, COUNT(sbpro_std));
            ok &= mix_set(base, ess_std, COUNT(ess_std));
            return ok;
        }
        if (!mix_present(base, SBP_VOICE, 0xEE))
            return -1;
        mix_save(base, sbpro_std, COUNT(sbpro_std));
        return mix_set(base, sbpro_std, COUNT(sbpro_std));
    case 6:
        if (!mix_present(base, SB16_VOICE, 0xF8))
            return -1;
        mix_save(base, sb16_std, COUNT(sb16_std));
        return mix_set(base, sb16_std, COUNT(sb16_std));
    }
    return -1;
}

void mixer_restore() {
    unsigned i;

    for (i = 0; i < saved_count; i++)
        mix_write(saved_base, saved_reg[i], saved_val[i]);
    saved_count = 0;
}
