#include <stdio.h>
#include "mdf.h"
#include "seq.h"
#include "vsync.h"
#include "opltone.h"
#include "sweep.h"
#include "pcm.h"
#include "host.h"

/*
 * TIMING
 *
 * Every key-on / key-off in the measured sequence is issued right
 * after vsync_wait() returns, with nothing slow in between.
 *
 */

/* The OPL channel the sequence plays on, and the next one for the
 * right side on an OPL3 */
#define FM_CHANNEL      0

#define PULSE_COUNT     10
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

/* OPL3: left and right on two channels */
int mdf_stereo;

/* Load the SINE instrument and the reference frequency
 * Call at least one frame before mdf_pulse_train()
 */
void mdf_pulse_prepare(uint8_t channel) {
    if (mdf_stereo)
        opl_set_pan(channel, OPL_PAN_CENTER);
    opl_set_instrument(channel, OPL_INSTRUMENT_SINE);
    opl_note_set(channel, REF_BLOCK, REF_FNUM);
}

/* OPL3-SAx, OPL3-L, OPL4 run from ~49516 Hz instead of 49716 Hz,
 * so it plays ~24 Hz lower at 6 kHz.
 *
 * Source: https://nerdlypleasures.blogspot.com/2018/01/opl23-frequency-1hz-ish-difference.html
 *
 * Values measured with MDFourier from real hardware, using -j for 1/16 Hz FFT
 * with peak interpolation (validated on a synthetic 6002.30 Hz tone):
 * ========================================================================
 * Card                         Fm Chip     Chip ID Tone    Estimated Clock
 * ========================================================================
 * Soundblaster 2.0 CT1350B     OPL2        6003.2305Hz     49725.4422Hz
 * SoundBlaster 16  CT1740      OPL3        6003.2335Hz     49725.4678Hz
 * SoundBlaster Vibra 16        CQM         6012.7485Hz     49804.2813Hz
 * Yamaha Audician 32 Plus      OPL3-SAx    5978.3358Hz     49519.2370Hz
 * Compaq ES1869                ESS ESFM    5991.0632Hz     49624.6592Hz
 * ESS Solo-1 ES1938            ESS ESFM    5990.1905Hz     49617.4307Hz
 * ========================================================================
 * The SB 2.0 and SB16 FM clocks seem to come from the ISA bus 14.31818 MHz
 * (/ 288 = 49715.9 Hz) so they might follow the PC, not the card.
 */

void mdf_tone_hold(uint8_t channel, unsigned frames) {
    if (seq_stopped())
        return;

    seq_add_frames(frames);
    opl_key_on(channel);
    while (frames-- && seq_frame())
        ;
    opl_note_off(channel);
}

/* Must be entered right after a vsync_wait(), with mdf_pulse_prepare()
 * already done.
 */
void mdf_pulse_train(uint8_t channel) {
    int i;

    if (seq_stopped())
        return;

    seq_add_frames(PULSE_COUNT * 2);
    for (i = 0; i < PULSE_COUNT; i++) {
        opl_key_on(channel);
        vsync_wait();
        opl_note_off(channel);
        vsync_wait();
    }
}

void mdf_sweep_prepare(uint8_t channel, opl_instrument_t instrument) {
    if (mdf_stereo) {
        opl_set_pan(channel, OPL_PAN_LEFT);
        opl_set_pan(channel + 1, OPL_PAN_RIGHT);
        opl_set_instrument(channel + 1, instrument);
    }
    opl_set_instrument(channel, instrument);
}

/* Left and right start one write apart */
void mdf_sweep_note_on(uint8_t channel, sweep_note_t *n, int set_mult) {
    if (set_mult) {
        opl_set_carrier_mult(channel, n->mult);
        if (mdf_stereo)
            opl_set_carrier_mult(channel + 1, n->mult);
    }
    if (mdf_stereo) {
        opl_note_set(channel, n->block, n->fnum);
        opl_note_set(channel + 1, n->block, n->fnum);
        opl_key_on(channel);
        opl_key_on(channel + 1);
    } else
        opl_note_on_raw(channel, n->block, n->fnum);
}

void mdf_sweep_note_off(uint8_t channel) {
    opl_note_off(channel);
    if (mdf_stereo)
        opl_note_off(channel + 1);
}

typedef enum {
    STEPS_SWEEP,        /* the notes, one per step */
    STEPS_DEPTH,        /* FM_DEPTH instrument: modulator level */
    STEPS_FEEDBACK,     /* FEEDBACK instrument: feedback 0-7 */
    STEPS_LEVEL,        /* SINE instrument: carrier level */
    STEPS_TREMOLO,      /* SINE instrument: tremolo */
    STEPS_VIBRATO,      /* SINE instrument: vibrato */
    STEPS_WAVE          /* SINE instrument: carrier waveform */
} steps_t;

typedef struct {
    uint8_t      channel;
    steps_t      kind;
    sweep_note_t *notes;        /* STEPS_SWEEP */
    int          set_mult;      /* STEPS_SWEEP: the sine sweep's carrier multiple */
} fm_block_t;

/* A note already keyed on. Key-off at 80%, the rest is its release */
void mdf_fm_step_off(void *context, int step) {
    fm_block_t *block = (fm_block_t *)context;

    (void)step;
    mdf_sweep_note_off(block->channel);
}

void mdf_fm_sweep_on(void *context, int step) {
    fm_block_t *block = (fm_block_t *)context;

    mdf_sweep_note_on(block->channel, &block->notes[step], block->set_mult);
}

void mdf_run_sweep(uint8_t channel, unsigned frames, sweep_note_t *notes, unsigned steps, int set_mult) {
    fm_block_t block;

    block.channel  = channel;
    block.kind     = STEPS_SWEEP;
    block.notes    = notes;
    block.set_mult = set_mult;
    seq_block((int)steps, frames, NULL, mdf_fm_sweep_on, mdf_fm_step_off, &block);
    mdf_sweep_note_off(channel);
}

#define COUNT(t) (sizeof(t) / sizeof(t[0]))

/* Modulator levels for the FM depth block: off (pure sine) to full */
uint8_t mdf_depth_level[] = { OPL_MOD_OFF, 48, 40, 32, 24, 16, 8, 0 };
#define DEPTH_STEPS COUNT(mdf_depth_level)
#define FEEDBACK_STEPS 8

/* Carrier levels for the level staircase: 0 to -42 dB in 6 dB steps */
uint8_t mdf_car_level[] = { 0, 8, 16, 24, 32, 40, 48, 56 };
#define LEVEL_STEPS COUNT(mdf_car_level)

/* Tremolo 1 and 4.8 dB, vibrato 7 and 14 cents */
uint8_t mdf_tremolo_depth[] = { 0, OPL_DEPTH_AM_DEEP };
uint8_t mdf_vibrato_depth[] = { 0, OPL_DEPTH_VIB_DEEP };

#define TREMOLO_STEPS COUNT(mdf_tremolo_depth)
#define VIBRATO_STEPS COUNT(mdf_vibrato_depth)

/* Carrier waveforms 0-7, OPL2 plays 4-7 as 0-3 */
#define WAVE_STEPS 8

void mdf_step_set(uint8_t channel, steps_t kind, unsigned step) {
    switch (kind) {
        case STEPS_DEPTH:
            opl_set_mod_level(channel, mdf_depth_level[step]);
            break;
        case STEPS_FEEDBACK:
            opl_set_feedback(channel, (uint8_t)step);
            break;
        case STEPS_LEVEL:
            opl_set_car_level(channel, mdf_car_level[step]);
            break;
        case STEPS_TREMOLO:
            opl_set_car_lfo(channel, OPL_LFO_AM);
            break;
        case STEPS_VIBRATO:
            opl_set_car_lfo(channel, OPL_LFO_VIB);
            break;
        case STEPS_WAVE:
            opl_set_car_wave(channel, (uint8_t)step);
            break;
        case STEPS_SWEEP:
            break;
    }
}

/* The 440 Hz tone, held once per step, a parameter changed before
 * each key-on: modulator level, feedback, carrier level, tremolo,
 * vibrato or carrier waveform.
 * Must be entered right after a vsync_wait(), instrument loaded */
sweep_note_t mdf_fm_tone = { TONE_BLOCK, TONE_FNUM, 1 };

void mdf_fm_steps_on(void *context, int step) {
    fm_block_t *block = (fm_block_t *)context;

    /* Global, shared by both channels */
    if (block->kind == STEPS_TREMOLO)
        opl_set_lfo_depth(mdf_tremolo_depth[step]);
    else if (block->kind == STEPS_VIBRATO)
        opl_set_lfo_depth(mdf_vibrato_depth[step]);
    mdf_step_set(block->channel, block->kind, (unsigned)step);
    if (mdf_stereo)
        mdf_step_set(block->channel + 1, block->kind, (unsigned)step);
    mdf_sweep_note_on(block->channel, &mdf_fm_tone, 0);
}

void mdf_run_steps(uint8_t channel, steps_t kind, unsigned steps, unsigned frames) {
    fm_block_t block;

    block.channel  = channel;
    block.kind     = kind;
    block.notes    = NULL;
    block.set_mult = 0;
    seq_block((int)steps, frames, NULL, mdf_fm_steps_on, mdf_fm_step_off, &block);
    mdf_sweep_note_off(channel);
    if (kind == STEPS_TREMOLO || kind == STEPS_VIBRATO)
        opl_set_lfo_depth(0);
}

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

void mdf_gap(uint8_t channel, unsigned frames, prep_t prep) {
    seq_silence(frames - 1);
    if (seq_stopped()) 
        prep = PREP_NONE;
    switch (prep) {
        case PREP_PULSES:
            mdf_pulse_prepare(channel);
            break;
        /* Under Windows id with 1500 and 2500 Hz */
        case PREP_WM_OK:
            if (host_is_windows())
                opl_note_set(channel, WM_WIN_OK_BLOCK, WM_WIN_OK_FNUM);
            else
                opl_note_set(channel, WM_OK_BLOCK, WM_OK_FNUM);
            break;
        case PREP_WM_BAD:
            if (host_is_windows())
                opl_note_set(channel, WM_WIN_BAD_BLOCK, WM_WIN_BAD_FNUM);
            else
                opl_note_set(channel, WM_BAD_BLOCK, WM_BAD_FNUM);
            break;
        case PREP_FM:
            mdf_sweep_prepare(channel, OPL_INSTRUMENT_DEFAULT);
            break;
        case PREP_SINE:
            mdf_sweep_prepare(channel, OPL_INSTRUMENT_SINE);
            break;
        case PREP_DEPTH:
            mdf_sweep_prepare(channel, OPL_INSTRUMENT_FM_DEPTH);
            break;
        case PREP_FEEDBACK:
            mdf_sweep_prepare(channel, OPL_INSTRUMENT_FEEDBACK);
            break;
        case PREP_PCM_CLOCK:
            pcm_clock_arm(PCMCLK_FRAMES);
            break;
        case PREP_NONE:
            break;
    }
    seq_silence(1);
}

/* One transfer, DMA loops the buffer */
void mdf_pcm_clock_block(unsigned frames) {
    if (seq_stopped())
        return;

    seq_add_frames(frames);
    pcm_clock_start();
    while (frames-- && seq_frame())
        ;
    pcm_clock_stop();
}

void mdf_pcm_steps(pcm_steps_t kind, unsigned steps, unsigned frames) {
    unsigned step, frame;

    for (step = 0; step < steps && !seq_stopped(); step++) {
        seq_add_frames(frames);
        pcm_step_start(kind, step & 1);
        if (step + 1 < steps)
            pcm_step_fill(kind, step + 1, (step + 1) & 1, frames);
        for (frame = 0; frame < frames; frame++)
            if (!seq_frame())
                break;
    }
    pcm_steps_end(kind);
}

/* Filler for the stereo / 16-bit blocks a card can't play, so the
 * recording doesn't go quiet for ~15 s and look finished.
 * MDFourier skips those types when their clock block has no tone */
#define FILLER_HZ       440
#define FILLER_LEVEL    20      /* carrier attenuation, 0.75 dB steps: -15 dB */
#define FILLER_DECAY    10

void mdf_filler_on(uint8_t channel) {
    if (mdf_stereo)
        opl_set_pan(channel, OPL_PAN_CENTER);
        
    opl_set_instrument(channel, OPL_INSTRUMENT_SINE);
    opl_set_car_level(channel, FILLER_LEVEL);
    opl_note_on(channel, FILLER_HZ);
}

void mdf_pcmx_steps(uint8_t channel, pcmx_kind_t kind, unsigned steps, unsigned frames, int next_kind) {
    unsigned step, frame;
    int      filler = !pcmx_enabled(kind) && kind != PCMX_CLK8 && kind != PCMX_CLK16;

    if (filler && !seq_stopped())
        mdf_filler_on(channel);

    for (step = 0; step < steps && !seq_stopped(); step++) {
        seq_add_frames(frames);
        pcmx_step_start(kind);
        if (step + 1 < steps)
            pcmx_step_fill(kind, step + 1);
        else if (next_kind >= 0)
            pcmx_step_fill((pcmx_kind_t)next_kind, 0);
        for (frame = 0; frame < frames; frame++) {
            if (filler && step + 1 == steps && frame + FILLER_DECAY == frames)
                opl_note_off(channel);
            if (!seq_frame())
                break;
        }
    }
    if (filler)
        opl_note_off(channel);
    if (next_kind < 0)
        pcmx_steps_end(kind);
}

int mdf_end_sequence(uint8_t channel) {
    int result;

    seq_stop();
    mdf_sweep_note_off(channel);
    pcm_finish();

    result = seq_report();
    if (!seq_stopped_late())
        pcm_print_report();
    return result;
}

int mdf_run_full_test(int use_stereo, int mixer_ok) {
    uint8_t channel = FM_CHANNEL;

    mdf_stereo = use_stereo;
    mdf_pulse_prepare(channel);

    /* Next PCM step, a chunk per frame */
    seq_begin(pcm_fill_more);

    /* MDF Sequence */

    /* Sync */
    mdf_pulse_train(channel);                                       

    /* CHIPID */
    mdf_gap(channel, SILENCE_FRAMES, PREP_NONE);
    mdf_tone_hold(channel, CHIPID_FRAMES);                          

    /* MIXER */
    mdf_gap(channel, SILENCE_FRAMES, mixer_ok ? PREP_WM_OK : PREP_WM_BAD);
    mdf_tone_hold(channel, WM_FRAMES);                              

    /* FM */
    mdf_gap(channel, SILENCE_FRAMES, PREP_FM);
    mdf_run_sweep(channel, SWEEP_FRAMES, sweep, SWEEP_STEPS, 0);
    seq_silence(DECAY_FRAMES);

    /* SINE */
    mdf_gap(channel, SILENCE_FRAMES, PREP_SINE);
    mdf_run_sweep(channel, SINE_FRAMES, sine_sweep, SINE_STEPS, 1);
    seq_silence(DECAY_FRAMES);

    /* DEPTH */
    mdf_gap(channel, SILENCE_FRAMES, PREP_DEPTH);
    mdf_run_steps(channel, STEPS_DEPTH, DEPTH_STEPS, STEP_FRAMES);         

    /* FEEDBACK, FEEDBACK7 */
    mdf_gap(channel, SILENCE_FRAMES, PREP_FEEDBACK);
    mdf_run_steps(channel, STEPS_FEEDBACK, FEEDBACK_STEPS, STEP_FRAMES);   

    /* LEVEL */
    mdf_gap(channel, SILENCE_FRAMES, PREP_SINE);
    mdf_run_steps(channel, STEPS_LEVEL, LEVEL_STEPS, STEP_FRAMES);         

    /* TREMOLO & VIBRATO with SINE instrument */
    mdf_gap(channel, SILENCE_FRAMES, PREP_SINE);
    mdf_run_steps(channel, STEPS_TREMOLO, TREMOLO_STEPS, TREMOLO_FRAMES);
    mdf_run_steps(channel, STEPS_VIBRATO, VIBRATO_STEPS, VIBRATO_FRAMES);

    /* WAVE */
    mdf_gap(channel, SILENCE_FRAMES, PREP_SINE);
    mdf_run_steps(channel, STEPS_WAVE, WAVE_STEPS, STEP_FRAMES);

    /* PCM, 8-bit mono */
    if (!seq_stopped())
        pcm_prepare();

    /* PCMCLK: rate / 8 */
    mdf_gap(channel, SILENCE_FRAMES, PREP_PCM_CLOCK);
    mdf_pcm_clock_block(PCMCLK_FRAMES);

    /* PCMIDLE: speaker on, nothing playing */
    pcm_step_fill(PCM_SWEEP, 0, 0, PCMSINE_FRAMES);
    seq_silence(PCMIDLE_FRAMES);

    /* PCMSINE */
    mdf_pcm_steps(PCM_SWEEP, PCM_SWEEP_STEPS, PCMSINE_FRAMES);

    /* PCMLEVEL */
    pcm_step_fill(PCM_LEVEL, 0, 0, PCMLEVEL_FRAMES);
    mdf_gap(channel, SILENCE_FRAMES, PREP_NONE);
    mdf_pcm_steps(PCM_LEVEL, PCM_LEVEL_STEPS, PCMLEVEL_FRAMES);

    /* PCM 8-bit stereo: DSP 3.xx and 4.xx, filler on older cards */
    if (!seq_stopped())
        pcmx_begin();
    mdf_gap(channel, SILENCE_FRAMES, PREP_NONE);
    mdf_pcmx_steps(channel, PCMX_CLK8, 1, PSCLK_FRAMES, PCMX_SEP8);
    mdf_pcmx_steps(channel, PCMX_SEP8, PCMX_SEP_STEPS, PSSEP_FRAMES, -1);

    /* PCM 16-bit stereo: DSP 4.xx with BLASTER H and ESS Extended mode, filler elsewhere */
    if (!seq_stopped())
        pcmx_begin16();
    mdf_gap(channel, SILENCE_FRAMES, PREP_NONE);
    mdf_pcmx_steps(channel, PCMX_CLK16, 1, P16CLK_FRAMES, PCMX_SWEEP16);
    mdf_pcmx_steps(channel, PCMX_SWEEP16, PCMX_SWEEP_STEPS, P16SINE_FRAMES, -1);
    pcmx_step_fill(PCMX_LEVEL16, 0);
    mdf_gap(channel, SILENCE_FRAMES, PREP_NONE);
    mdf_pcmx_steps(channel, PCMX_LEVEL16, PCMX_LEVEL_STEPS, P16LEVEL_FRAMES, PCMX_SEP16);
    mdf_pcmx_steps(channel, PCMX_SEP16, PCMX_SEP_STEPS, P16SEP_FRAMES, -1);

    /* Sync */
    mdf_gap(channel, SILENCE_FRAMES, PREP_PULSES);
    mdf_pulse_train(channel);                                       

    return mdf_end_sequence(channel);
}
