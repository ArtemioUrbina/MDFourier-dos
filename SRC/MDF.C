#include <stdio.h>
#include "mdf.h"
#include "seq.h"
#include "fmblk.h"
#include "pcmblk.h"
#include "pcm.h"

/*
 * TIMING
 *
 * Every key-on / key-off in the measured sequence is issued right
 * after vsync_wait() returns, with nothing slow in between.
 *
 */

#define SWEEP_FRAMES    20
#define SILENCE_FRAMES  20
#define DECAY_FRAMES    5
#define CHIPID_FRAMES   60
#define WM_FRAMES       10
#define SINE_FRAMES     10
#define STEP_FRAMES     20
/* The LFOs run freely and can't be reset. Compare as waveforms atm */
#define TREMOLO_FRAMES  70
#define VIBRATO_FRAMES  70
#define PCMCLK_FRAMES   60
#define PCMIDLE_FRAMES  20
#define PCMSINE_FRAMES  10
#define PCMLEVEL_FRAMES 20
#define PSCLK_FRAMES    60      /* 8-bit stereo */
#define PSSEP_FRAMES    20
#define P16CLK_FRAMES   60      /* 16-bit stereo */
#define P16SINE_FRAMES  10
#define P16LEVEL_FRAMES 20
#define P16SEP_FRAMES   20


/* What a silence loads in its last frame, so the next block's first
 * key-on lands right on a frame edge */
typedef enum {
    PREP_NONE,
    PREP_PULSES,        /* SINE, reference note, centered */
    PREP_WM_OK,         /* watermark note: mixer verified */
    PREP_WM_BAD,        /* watermark note: mixer not verified */
    PREP_FM,            /* sweep instruments, left/right in stereo */
    PREP_SINE,          /* sweep sine */
    PREP_DEPTH,         /* FM depth: modulator level */
    PREP_FEEDBACK,      /* feedback steps */
    PREP_PCM_CLOCK      /* PCM rate and DMA for the clock tone */
} prep_t;

void mdf_gap(unsigned frames, prep_t prep) {
    seq_silence(frames - 1);
    if (seq_stopped())
        prep = PREP_NONE;
    switch (prep) {
        case PREP_PULSES:
            fmblk_ref_prepare();
            break;
        case PREP_WM_OK:
            fmblk_watermark_prepare(1);
            break;
        case PREP_WM_BAD:
            fmblk_watermark_prepare(0);
            break;
        case PREP_FM:
            fmblk_instrument(OPL_INSTRUMENT_DEFAULT);
            break;
        case PREP_SINE:
            fmblk_instrument(OPL_INSTRUMENT_SINE);
            break;
        case PREP_DEPTH:
            fmblk_instrument(OPL_INSTRUMENT_FM_DEPTH);
            break;
        case PREP_FEEDBACK:
            fmblk_instrument(OPL_INSTRUMENT_FEEDBACK);
            break;
        case PREP_PCM_CLOCK:
            pcm_clock_arm(PCMCLK_FRAMES);
            break;
        case PREP_NONE:
            break;
    }
    seq_silence(1);
}

/* The stereo and 16-bit blocks a card can't play get a filler, so the
 * recording doesn't go quiet for ~15 s and look finished.
 * MDFourier skips those types when their clock block has no tone */
#define FILLER_DECAY    10

void mdf_pcmx_steps(pcmx_kind_t kind, unsigned steps, unsigned frames, int next_kind) {
    unsigned step, frame;
    int      filler = !pcmx_enabled(kind) && kind != PCMX_CLK8 && kind != PCMX_CLK16;

    if (filler && !seq_stopped())
        fmblk_filler_on();

    for (step = 0; step < steps && !seq_stopped(); step++) {
        seq_add_frames(frames);
        pcmx_step_start(kind);
        if (step + 1 < steps)
            pcmx_step_fill(kind, step + 1);
        else if (next_kind >= 0)
            pcmx_step_fill((pcmx_kind_t)next_kind, 0);
        for (frame = 0; frame < frames; frame++) {
            if (filler && step + 1 == steps && frame + FILLER_DECAY == frames)
                fmblk_filler_off();
            if (!seq_frame())
                break;
        }
    }
    if (filler)
        fmblk_filler_off();
    if (next_kind < 0)
        pcmx_steps_end(kind);
}

int mdf_end_sequence() {
    int result;

    seq_stop();
    fmblk_off();
    pcm_finish();

    result = seq_report();
    if (!seq_stopped_late())
        pcm_print_report();
    return result;
}

int mdf_run_full_test(caps_t *caps) {
    /* The OPL3 plays the right side on its own channel */
    fmblk_init(caps->fm == CAPS_FM_OPL3);
    fmblk_ref_prepare();

    /* Next PCM step, a chunk per frame */
    seq_begin(pcm_fill_more);

    /* MDF Sequence */

    /* Sync */
    fmblk_pulse_train();

    /* CHIPID */
    mdf_gap(SILENCE_FRAMES, PREP_NONE);
    fmblk_tone(CHIPID_FRAMES);

    /* MIXER */
    mdf_gap(SILENCE_FRAMES, caps->mixer_ok ? PREP_WM_OK : PREP_WM_BAD);
    fmblk_tone(WM_FRAMES);

    /* FM */
    mdf_gap(SILENCE_FRAMES, PREP_FM);
    fmblk_sweep(SWEEP_FRAMES, 1);
    seq_silence(DECAY_FRAMES);

    /* SINE */
    mdf_gap(SILENCE_FRAMES, PREP_SINE);
    fmblk_sine_sweep(SINE_FRAMES);
    seq_silence(DECAY_FRAMES);

    /* DEPTH */
    mdf_gap(SILENCE_FRAMES, PREP_DEPTH);
    fmblk_steps(FMBLK_DEPTH, FMBLK_DEPTH_STEPS, STEP_FRAMES);

    /* FEEDBACK, FEEDBACK7 */
    mdf_gap(SILENCE_FRAMES, PREP_FEEDBACK);
    fmblk_steps(FMBLK_FEEDBACK, FMBLK_FEEDBACK_STEPS, STEP_FRAMES);

    /* LEVEL */
    mdf_gap(SILENCE_FRAMES, PREP_SINE);
    fmblk_steps(FMBLK_LEVEL, FMBLK_LEVEL_STEPS, STEP_FRAMES);

    /* TREMOLO & VIBRATO with SINE instrument */
    mdf_gap(SILENCE_FRAMES, PREP_SINE);
    fmblk_steps(FMBLK_TREMOLO, FMBLK_TREMOLO_STEPS, TREMOLO_FRAMES);
    fmblk_steps(FMBLK_VIBRATO, FMBLK_VIBRATO_STEPS, VIBRATO_FRAMES);

    /* WAVE */
    mdf_gap(SILENCE_FRAMES, PREP_SINE);
    fmblk_steps(FMBLK_WAVE, FMBLK_WAVE_STEPS, STEP_FRAMES);

    /* PCM, 8-bit mono */
    if (!seq_stopped())
        pcm_prepare();

    /* PCMCLK: rate / 8 */
    mdf_gap(SILENCE_FRAMES, PREP_PCM_CLOCK);
    pcmblk_clock(PCMCLK_FRAMES);

    /* PCMIDLE: speaker on, nothing playing */
    pcm_step_fill(PCM_SWEEP, 0, 0, PCMSINE_FRAMES);
    seq_silence(PCMIDLE_FRAMES);

    /* PCMSINE */
    pcmblk_steps(PCM_SWEEP, PCM_SWEEP_STEPS, PCMSINE_FRAMES);

    /* PCMLEVEL */
    pcm_step_fill(PCM_LEVEL, 0, 0, PCMLEVEL_FRAMES);
    mdf_gap(SILENCE_FRAMES, PREP_NONE);
    pcmblk_steps(PCM_LEVEL, PCM_LEVEL_STEPS, PCMLEVEL_FRAMES);

    /* PCM 8-bit stereo: DSP 3.xx and 4.xx, filler on older cards */
    if (!seq_stopped())
        pcmx_begin();
    mdf_gap(SILENCE_FRAMES, PREP_NONE);
    mdf_pcmx_steps(PCMX_CLK8, 1, PSCLK_FRAMES, PCMX_SEP8);
    mdf_pcmx_steps(PCMX_SEP8, PCMX_SEP_STEPS, PSSEP_FRAMES, -1);

    /* PCM 16-bit stereo: DSP 4.xx with BLASTER H and ESS Extended mode, filler elsewhere */
    if (!seq_stopped())
        pcmx_begin16();
    mdf_gap(SILENCE_FRAMES, PREP_NONE);
    mdf_pcmx_steps(PCMX_CLK16, 1, P16CLK_FRAMES, PCMX_SWEEP16);
    mdf_pcmx_steps(PCMX_SWEEP16, PCMX_SWEEP_STEPS, P16SINE_FRAMES, -1);
    pcmx_step_fill(PCMX_LEVEL16, 0);
    mdf_gap(SILENCE_FRAMES, PREP_NONE);
    mdf_pcmx_steps(PCMX_LEVEL16, PCMX_LEVEL_STEPS, P16LEVEL_FRAMES, PCMX_SEP16);
    mdf_pcmx_steps(PCMX_SEP16, PCMX_SEP_STEPS, P16SEP_FRAMES, -1);

    /* Sync */
    mdf_gap(SILENCE_FRAMES, PREP_PULSES);
    fmblk_pulse_train();

    return mdf_end_sequence();
}
