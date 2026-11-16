#ifndef SEQ_H
#define SEQ_H

/* Frame engine for the sequences. A late frame stops the run (counted
 * only under Windows), ESC aborts */

#define SEQ_OK          0   /* complete, every frame on time */
#define SEQ_ABORTED     1   /* ESC pressed */
#define SEQ_BAD_FRAMES  2   /* a frame out of tolerance */

/* Block step callbacks, context passed back as is */
typedef void (*seq_step_fn)(void *context, int step);

/* Aligns to the retrace, frame_hook runs before every wait or NULL */
void seq_begin(void (*frame_hook)());

/* One frame: 0 when the run stopped, late or by ESC */
int  seq_frame();
/* The run stopped, as of the last frame */
int  seq_stopped();

/* Frames the blocks should play, checked against the retrace count.
 * A block using vsync_wait() directly adds its own */
void seq_add_frames(unsigned frames);

void seq_silence(unsigned frames);

/* on() at the start, off() at 80%, prep() for the next step in the
 * silence. prep and off can be NULL. Call right after a frame edge */
void seq_block(int steps, unsigned frames, seq_step_fn prep, seq_step_fn on,
               seq_step_fn off, void *context);

/* Stops the frame timing, before silencing the chips */
void seq_stop();
/* Prints how the run went: SEQ_OK, SEQ_ABORTED or SEQ_BAD_FRAMES */
int  seq_report();
/* Stopped at a late frame, nothing else to report */
int  seq_stopped_late();

#endif
