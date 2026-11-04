#ifndef SEQ_H
#define SEQ_H

/* Frame engine shared by the sequences. Every frame starts right after
 * the retrace, a late frame stops the run (under Windows we only count
 * it) and ESC aborts it */

#define SEQ_OK          0   /* complete, every frame on time */
#define SEQ_ABORTED     1   /* ESC pressed */
#define SEQ_BAD_FRAMES  2   /* a frame out of tolerance, capture unusable */

/* What a block does with each step: prepare it ahead, start it on the
 * frame edge, stop it. context is the caller's, passed back as is */
typedef void (*seq_step_fn)(void *context, int step);

/* Aligns to the retrace. frame_hook runs before every wait, NULL if
 * the sequence needs none */
void seq_begin(void (*frame_hook)());

/* One frame: 0 when the run stopped, late or by ESC */
int  seq_frame();
/* The run stopped, as of the last frame */
int  seq_stopped();

/* Frames the blocks are meant to play, checked against the frames the
 * retrace counted. The engine's own calls add theirs, a block played
 * with raw vsync_wait() adds its own */
void seq_add_frames(unsigned frames);

void seq_silence(unsigned frames);

/* steps of frames each. on() at the start, off() at 80% and, while
 * the rest is silent, prep() for the next step. prep and off can be
 * NULL. Must be entered right after a frame edge */
void seq_block(int steps, unsigned frames, seq_step_fn prep, seq_step_fn on,
               seq_step_fn off, void *context);

/* Stops the frame timing, before silencing the chips */
void seq_stop();
/* Prints how the run went: SEQ_OK, SEQ_ABORTED or SEQ_BAD_FRAMES */
int  seq_report();
/* The run stopped at a late frame, there is nothing else to report */
int  seq_stopped_late();

#endif
