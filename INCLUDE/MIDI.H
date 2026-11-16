#ifndef MIDI_H
#define MIDI_H

#include <stdint.h>

/* MPU-401 in UART mode */

#define MIDI_PORT_DEFAULT   0x330

/* Controllers */
#define MIDI_CC_MODULATION  1
#define MIDI_CC_VOLUME      7
#define MIDI_CC_PAN         10
#define MIDI_CC_EXPRESSION  11
#define MIDI_CC_HOLD        64
#define MIDI_CC_REVERB      91
#define MIDI_CC_CHORUS      93
#define MIDI_CC_VARIATION   94
#define MIDI_CC_SOUND_OFF   120
#define MIDI_CC_RESET_ALL   121
#define MIDI_CC_NOTES_OFF   123

extern uint16_t midi_port;

/* Reset, then UART mode, 1 when there */
int  midi_init(uint16_t port);
/* Didn't answer, some UART only interfaces don't: we send anyway */
int  midi_uart_unconfirmed();
/* Notes and sound off, UART mode off. For atexit() */
void midi_exit();

/* 0 if it didn't take the byte in time, counted as lost */
int  midi_send(uint8_t byte);
void midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
void midi_note_off(uint8_t channel, uint8_t note);
void midi_control(uint8_t channel, uint8_t control, uint8_t value);
void midi_program(uint8_t channel, uint8_t program);
void midi_pitch_center(uint8_t channel);
/* A whole message, F0h to F7h */
void midi_sysex(uint8_t *data, unsigned length);

/* Bytes the MPU-401 didn't take */
unsigned midi_lost();

#endif
