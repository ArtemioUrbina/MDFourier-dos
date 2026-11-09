#include <stdio.h>
#include "sb2seq.h"
#include "seq.h"
#include "vsync.h"
#include "fmblk.h"
#include "cmsblk.h"
#include "pcmblk.h"
#include "cms.h"

/*
 * CMS sequence (/C), its own profile: DOS: Sound Blaster 2.0 and CMS
 *
 * FM, CMS, 8-bit mono PCM and ADPCM, for the Sound Blaster 2.0 and
 * earlier cards, the Game Blaster, and the ADPCM of the later ones. It plays the same timeline on every
 * card: each source starts with its clock block, and a source the card
 * doesn't have leaves its clock block silent and plays a filler in its
 * other blocks, so the recording doesn't go quiet and look finished.
 * MDFourier skips the types of an optional clock whose tone is missing.
 *
 * Sync and watermark come from FM when there is one, from CMS on a Game
 * Blaster. The CMS blocks load each step ahead of it at level 0, so
 * their first step goes in at the start of the silence before them.
 */

#define SILENCE_FRAMES  20
#define PAD_FRAMES      20
#define LEAD_FRAMES     4           /* CMS sync tone loaded before the first pulse */
#define SETTLE_FRAMES   40          /* the chord's thump, before the ADPCM clock */
#define DECAY_FRAMES    5
#define CLK_FRAMES      60
#define WM_FRAMES       10
#define FM_STEP_FRAMES  20
#define FM_SWEEP_STRIDE 4           /* every 4th note of the FM sweep, 32 steps */
#define CMS_STEP_FRAMES 20
#define CMS_LONG_FRAMES 40          /* noise and envelopes: more bins per band */
#define CHORD_FRAMES    60
#define PCMIDLE_FRAMES  20
#define PCMSINE_FRAMES  10
#define PCMLEVEL_FRAMES 20
#define ADPCMSINE_FRAMES  10
#define ADPCMLEVEL_FRAMES 20
#define FILLER_DECAY    10

/* What each source does on this card */
int sb2seq_fm;          /* FM plays its blocks */
int sb2seq_cms;         /* CMS plays its blocks */
int sb2seq_pcm;         /* 8-bit PCM plays its blocks */
int sb2seq_adpcm;       /* ADPCM plays its blocks */
int sb2seq_mixer_ok;

/* What a silence loads in its last frame, so the next block's first
 * start lands right on a frame edge. The CMS blocks are loaded at the
 * start of the silence instead, by sb2seq_cms_gap() */
typedef enum {
    PREP_NONE,
    PREP_FM_REF,        /* FM SINE, reference note, centered */
    PREP_FM_SWEEP,      /* FM sweep instruments */
    PREP_FM_SINE,       /* FM level steps */
    PREP_PCM_CLOCK,     /* PCM rate and DMA for the clock tone */
    PREP_ADPCM_CLOCK    /* ADPCM rate and DMA for its clock tone */
} prep_t;

void sb2seq_gap(unsigned frames, prep_t prep) {
    seq_silence(frames - 1);
    if (seq_stopped())
        prep = PREP_NONE;
    switch (prep) {
        case PREP_FM_REF:
            if (sb2seq_fm)
                fmblk_ref_prepare();
            break;
        case PREP_FM_SWEEP:
            if (sb2seq_fm)
                fmblk_instrument(OPL_INSTRUMENT_DEFAULT);
            break;
        case PREP_FM_SINE:
            if (sb2seq_fm)
                fmblk_instrument(OPL_INSTRUMENT_SINE);
            break;
        case PREP_PCM_CLOCK:
            pcm_clock_arm(CLK_FRAMES);
            break;
        case PREP_ADPCM_CLOCK:
            pcm_adpcm_clock_arm();
            break;
        case PREP_NONE:
            break;
    }
    seq_silence(1);
}

/* Silence with the CMS block's first step loaded at its start */
void sb2seq_cms_gap(unsigned frames, cmsblk_kind_t kind) {
    if (sb2seq_cms)
        cmsblk_prepare(kind);
    seq_silence(frames);
}

/* ---- Sync and watermark: FM, or CMS on a Game Blaster ---- */

void sb2seq_ref_prepare() {
    if (seq_stopped())
        return;
    if (sb2seq_fm)
        fmblk_ref_prepare();
    else
        cmsblk_ref_prepare();
}

void sb2seq_watermark_prepare() {
    if (seq_stopped())
        return;
    if (sb2seq_fm)
        fmblk_watermark_prepare(sb2seq_mixer_ok);
    else
        cmsblk_watermark_prepare(sb2seq_mixer_ok);
}

void sb2seq_pulse_train() {
    if (sb2seq_fm)
        fmblk_pulse_train();
    else
        cmsblk_pulse_train();
}

void sb2seq_tone(unsigned frames) {
    if (sb2seq_fm)
        fmblk_tone(frames);
    else
        cmsblk_tone(frames);
}

/* ---- Filler: FM when there is one, CMS on a Game Blaster ---- */

void sb2seq_filler(unsigned steps, unsigned frames) {
    unsigned total = steps * frames;

    if (seq_stopped())
        return;
    if (sb2seq_fm)
        fmblk_filler_on();
    else
        cmsblk_filler_on();
    seq_silence(total - FILLER_DECAY);
    if (sb2seq_fm)
        fmblk_filler_off();
    else
        cmsblk_filler_off();
    seq_silence(FILLER_DECAY);
}

/* Clock blocks of a missing source stay silent, MDFourier skips its types */

void sb2seq_fm_steps(fmblk_steps_t kind, unsigned steps, unsigned frames) {
    if (sb2seq_fm)
        fmblk_steps(kind, steps, frames);
    else
        sb2seq_filler(steps, frames);
}

void sb2seq_cms_steps(cmsblk_kind_t kind, unsigned steps, unsigned frames) {
    if (sb2seq_cms)
        cmsblk_steps(kind, steps, frames);
    else
        sb2seq_filler(steps, frames);
}

void sb2seq_pcm_steps(pcm_steps_t kind, unsigned steps, unsigned frames) {
    if (sb2seq_pcm)
        pcmblk_steps(kind, steps, frames);
    else
        sb2seq_filler(steps, frames);
}

/* A format's sweep and levels, each first step built during the
 * silence before it */
void sb2seq_adpcm_format(pcm_adpcm_t format) {
    pcm_adpcm_step_fill(format, PCM_SWEEP, 0, 0, ADPCMSINE_FRAMES);
    sb2seq_gap(PAD_FRAMES, PREP_NONE);
    if (sb2seq_adpcm)
        pcmblk_adpcm_steps(format, PCM_SWEEP, PCM_ADPCM_SWEEP_STEPS, ADPCMSINE_FRAMES);
    else
        sb2seq_filler(PCM_ADPCM_SWEEP_STEPS, ADPCMSINE_FRAMES);

    pcm_adpcm_step_fill(format, PCM_LEVEL, 0, 0, ADPCMLEVEL_FRAMES);
    sb2seq_gap(PAD_FRAMES, PREP_NONE);
    if (sb2seq_adpcm)
        pcmblk_adpcm_steps(format, PCM_LEVEL, PCM_ADPCM_LEVEL_STEPS, ADPCMLEVEL_FRAMES);
    else
        sb2seq_filler(PCM_ADPCM_LEVEL_STEPS, ADPCMLEVEL_FRAMES);
}

int sb2seq_run(caps_t *caps) {
    int i, result;

    sb2seq_fm       = caps->fm != CAPS_FM_NONE;
    sb2seq_cms      = caps->cms != CMS_NONE;
    sb2seq_pcm      = caps->pcm;
    sb2seq_adpcm    = caps->adpcm;
    sb2seq_mixer_ok = caps->mixer_ok;

    if (sb2seq_fm)
        fmblk_init(caps->fm == CAPS_FM_OPL3);
    if (sb2seq_cms)
        cms_reset();

    /* Next PCM step, a chunk per frame */
    seq_begin(sb2seq_pcm ? pcm_fill_more : NULL);

    /* The sync tone loaded before the first pulse, a CMS one running at level 0 */
    sb2seq_ref_prepare();
    seq_add_frames(LEAD_FRAMES);
    for (i = 0; i < LEAD_FRAMES; i++)
        vsync_wait();

    /* Sync, padded for the CMS pulses' thump */
    sb2seq_pulse_train();
    seq_silence(PAD_FRAMES);

    /* MIXER */
    sb2seq_watermark_prepare();
    seq_silence(SILENCE_FRAMES);
    sb2seq_tone(WM_FRAMES);

    /* FMCLK: chip ID */
    sb2seq_gap(PAD_FRAMES, PREP_FM_REF);
    if (sb2seq_fm)
        fmblk_tone(CLK_FRAMES);
    else
        seq_silence(CLK_FRAMES);

    /* FM: every 4th note of the sweep */
    sb2seq_gap(PAD_FRAMES, PREP_FM_SWEEP);
    if (sb2seq_fm)
        fmblk_sweep(FM_STEP_FRAMES, FM_SWEEP_STRIDE);
    else
        sb2seq_filler(fmblk_sweep_steps(FM_SWEEP_STRIDE), FM_STEP_FRAMES);
    seq_silence(DECAY_FRAMES);

    /* FM LEVEL */
    sb2seq_gap(SILENCE_FRAMES, PREP_FM_SINE);
    sb2seq_fm_steps(FMBLK_LEVEL, FMBLK_LEVEL_STEPS, FM_STEP_FRAMES);

    /* CMSCLK: the CMS reference tone, chip ID */
    if (sb2seq_cms)
        cmsblk_ref_prepare();
    seq_silence(PAD_FRAMES);
    if (sb2seq_cms)
        cmsblk_tone(CLK_FRAMES);
    else
        seq_silence(CLK_FRAMES);

    sb2seq_cms_gap(PAD_FRAMES, CMSBLK_NOTES);
    sb2seq_cms_steps(CMSBLK_NOTES, CMSBLK_NOTES_STEPS, CMS_STEP_FRAMES);
    sb2seq_cms_gap(PAD_FRAMES, CMSBLK_LEVELS);
    sb2seq_cms_steps(CMSBLK_LEVELS, CMSBLK_LEVELS_STEPS, CMS_STEP_FRAMES);
    sb2seq_cms_gap(PAD_FRAMES, CMSBLK_VOICES);
    sb2seq_cms_steps(CMSBLK_VOICES, CMSBLK_VOICES_STEPS, CMS_STEP_FRAMES);
    sb2seq_cms_gap(PAD_FRAMES, CMSBLK_SEPARATION);
    sb2seq_cms_steps(CMSBLK_SEPARATION, CMSBLK_SEPARATION_STEPS, CMS_STEP_FRAMES);
    sb2seq_cms_gap(PAD_FRAMES, CMSBLK_NOISE);
    sb2seq_cms_steps(CMSBLK_NOISE, CMSBLK_NOISE_STEPS, CMS_LONG_FRAMES);
    sb2seq_cms_gap(PAD_FRAMES, CMSBLK_ENVELOPE);
    sb2seq_cms_steps(CMSBLK_ENVELOPE, CMSBLK_ENVELOPE_STEPS, CMS_LONG_FRAMES);
    sb2seq_cms_gap(PAD_FRAMES, CMSBLK_STACK);
    sb2seq_cms_steps(CMSBLK_STACK, CMSBLK_STACK_STEPS, CMS_STEP_FRAMES);
    sb2seq_cms_gap(PAD_FRAMES, CMSBLK_CHORD);
    if (sb2seq_cms)
        cmsblk_chord(CHORD_FRAMES);
    else
        sb2seq_filler(1, CHORD_FRAMES);

    /* ADPCMCLK: 4-bit at rate / 8, encoded during the silence. The
     * speaker goes on at its start, the SB 1.x/2.0 takes up to ~112 ms */
    if (!seq_stopped()) {
        pcm_prepare();
        pcm_adpcm_clock_build(CLK_FRAMES);
    }
    sb2seq_gap(SETTLE_FRAMES, PREP_ADPCM_CLOCK);
    if (sb2seq_adpcm)
        pcmblk_adpcm_clock(CLK_FRAMES);
    else
        seq_silence(CLK_FRAMES);

    /* ADPCM4, ADPCM3 (2.6-bit), ADPCM2: sweep and levels each. Before
     * the 8-bit PCM: their distortion reaches the sync tone's frequency,
     * so the last block before the closing pulses stays a clean one */
    sb2seq_adpcm_format(PCM_ADPCM4);
    sb2seq_adpcm_format(PCM_ADPCM3);
    sb2seq_adpcm_format(PCM_ADPCM2);

    /* PCMCLK: rate / 8 */
    sb2seq_gap(PAD_FRAMES, PREP_PCM_CLOCK);
    if (sb2seq_pcm)
        pcmblk_clock(CLK_FRAMES);
    else
        seq_silence(CLK_FRAMES);

    /* PCMIDLE: speaker on, nothing playing */
    pcm_step_fill(PCM_SWEEP, 0, 0, PCMSINE_FRAMES);
    seq_silence(PCMIDLE_FRAMES);

    /* PCMSINE */
    sb2seq_pcm_steps(PCM_SWEEP, PCM_SWEEP_STEPS, PCMSINE_FRAMES);

    /* PCMLEVEL */
    pcm_step_fill(PCM_LEVEL, 0, 0, PCMLEVEL_FRAMES);
    sb2seq_gap(SILENCE_FRAMES, PREP_NONE);
    sb2seq_pcm_steps(PCM_LEVEL, PCM_LEVEL_STEPS, PCMLEVEL_FRAMES);

    /* Sync */
    sb2seq_ref_prepare();
    seq_silence(SILENCE_FRAMES);
    sb2seq_pulse_train();

    seq_stop();
    if (sb2seq_fm)
        fmblk_off();
    if (sb2seq_cms)
        cmsblk_off();
    pcm_finish();

    result = seq_report();
    if (!seq_stopped_late())
        pcm_print_report();
    return result;
}
