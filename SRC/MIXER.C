#include <stdio.h>
#include <conio.h>
#include "mixer.h"

/* Mixer index/data ports, relative to the BLASTER port */
#define MIX_INDEX   4
#define MIX_DATA    5

/* SB Pro (CT1345) registers: left in bits 7-4, right in 3-0 */
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
#define SB16_OUTSW  0x3C        /* bit0 mic, 1-2 CD R/L, 3-4 line R/L */
#define SB16_GAIN   0x41        /* L, R at +1: bits 7-6, x1 x2 x4 x8 */
#define SB16_TREBLE 0x44        /* L, R at +1: bits 7-4, 8 = flat */
#define SB16_BASS   0x46

/* Mixer values */
typedef struct {
    uint8_t reg;
    uint8_t mask;
    uint8_t value;
} mix_std_t;

static const mix_std_t sb16_std[] = {
    { SB16_MASTER,     0xF8, 0xF8 },    /* 0 dB */
    { SB16_MASTER + 1, 0xF8, 0xF8 },
    { SB16_FM,         0xF8, 0xF8 },
    { SB16_FM + 1,     0xF8, 0xF8 },
    { SB16_GAIN,       0xC0, 0x00 },    /* x1 */
    { SB16_GAIN + 1,   0xC0, 0x00 },
    { SB16_TREBLE,     0xF0, 0x80 },    /* flat */
    { SB16_TREBLE + 1, 0xF0, 0x80 },
    { SB16_BASS,       0xF0, 0x80 },
    { SB16_BASS + 1,   0xF0, 0x80 },
    { SB16_OUTSW,      0x1F, 0x00 }     /* no CD, line, mic */
};

static const mix_std_t sbpro_std[] = {
    { SBP_MASTER,      0xEE, 0xEE },    /* max */
    { SBP_FM,          0xEE, 0xEE },
    { SBP_CD,          0xEE, 0x00 },    /* muted */
    { SBP_LINE,        0xEE, 0x00 },
    { SBP_MIC,         0x06, 0x00 }
};

/* Previous state, for mixer_restore() */
static uint16_t         saved_base;
static const mix_std_t *saved_std;
static unsigned         saved_count;
static uint8_t          saved_val[16];

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

int sb16_db(uint8_t val) {
    return 2 * ((val >> 3) - 31);
}

int report_sb16(uint16_t base) {
    uint8_t mstl, mstr, fml, fmr, outsw, gl, gr, tl, tr, bl, br, mic;
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

    printf("Mixer: SB16, master %d/%d dB, FM %d/%d dB, gain x%d/x%d\n",
           sb16_db(mstl), sb16_db(mstr), sb16_db(fml), sb16_db(fmr),
           1 << gl, 1 << gr);
    printf("       treble %u/%u, bass %u/%u (8 = flat), to output:%s%s%s%s\n",
           tl, tr, bl, br,
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
    } else if (tl != 8 || tr != 8 || bl != 8 || br != 8) {
        printf("  WARNING: treble/bass not flat, alters the frequency response\n");
        warn = 1;
    }
    /* CD, line, mic with any volume */
    if ((outsw & 0x1E) || ((outsw & 0x01) && mic)) {
        printf("  WARNING: other inputs mixed into the output, adds noise\n");
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

    printf("Mixer: SB Pro, master %u/%u, FM %u/%u (15 = max)\n",
           mst >> 4, mst & 0x0F, fm >> 4, fm & 0x0F);
    printf("       CD %u/%u, line %u/%u, mic %u/3\n",
           cd >> 4, cd & 0x0F, line >> 4, line & 0x0F, mic);

    /* bit 0 of each nibble is unused, 14 or 15 both mean max */
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
        if (mix_present(base, SBP_VOICE, 0xEE))
            return(report_sbpro(base));
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

/* Save, write and verify */
static int mix_apply(uint16_t base, const mix_std_t *std, unsigned count) {
    unsigned i;
    int ok = 1;

    saved_base  = base;
    saved_std   = std;
    saved_count = count;

    for (i = 0; i < count; i++) {
        saved_val[i] = mix_read(base, std[i].reg);
        mix_write(base, std[i].reg,
                  (uint8_t)((saved_val[i] & ~std[i].mask) | std[i].value));
    }
    for (i = 0; i < count; i++) {
        if ((mix_read(base, std[i].reg) & std[i].mask) != std[i].value)
            ok = 0;
    }
    return ok;
}

int mixer_set_standard(blaster_cfg_t *cfg) {
    uint16_t base;

    if (cfg->port == -1 || cfg->type == -1)
        return -1;
    base = (uint16_t)cfg->port;

    switch (cfg->type) {
    case 2:
    case 4:
    case 5:
        if (!mix_present(base, SBP_VOICE, 0xEE))
            return -1;
        return mix_apply(base, sbpro_std, sizeof(sbpro_std) / sizeof(sbpro_std[0]));
    case 6:
        if (!mix_present(base, SB16_VOICE, 0xF8))
            return -1;
        return mix_apply(base, sb16_std, sizeof(sb16_std) / sizeof(sb16_std[0]));
    }
    return -1;
}

void mixer_restore() {
    unsigned i;

    for (i = 0; i < saved_count; i++)
        mix_write(saved_base, saved_std[i].reg, saved_val[i]);
    saved_count = 0;
}
