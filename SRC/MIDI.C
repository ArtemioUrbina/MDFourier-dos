#include <conio.h>
#include "midi.h"
#include "pit.h"

/* MPU-401: data at base, status and command at base + 1. Status bit 6:
 * busy, bit 7: nothing to read */
#define MIDI_DATA       0
#define MIDI_STATUS     1
#define MIDI_DRR        0x40
#define MIDI_DSR        0x80

#define MIDI_CMD_RESET  0xFF
#define MIDI_CMD_UART   0x3F
#define MIDI_ACK        0xFE

#define MIDI_NOTE_OFF   0x80
#define MIDI_NOTE_ON    0x90
#define MIDI_CONTROL    0xB0
#define MIDI_PROGRAM    0xC0
#define MIDI_BEND       0xE0

/* A byte is 320 us at 31250 baud */
#define MIDI_TIMEOUT        PIT_US_TO_CLK(10000UL)
#define MIDI_ACK_TIMEOUT    PIT_US_TO_CLK(50000UL)

uint16_t midi_port = MIDI_PORT_DEFAULT;
int      midi_ready;
int      midi_uart_unacked;
unsigned midi_lost_bytes;

/* Read and drop MIDI IN, a full input buffer holds the output on some
 * interfaces */
void midi_drain() {
    int count = 0;

    while (!(inp(midi_port + MIDI_STATUS) & MIDI_DSR) && count++ < 256)
        inp(midi_port + MIDI_DATA);
}

int midi_wait_ready() {
    uint32_t start = pit_now();

    while (inp(midi_port + MIDI_STATUS) & MIDI_DRR) {
        midi_drain();
        if ((uint32_t)(pit_now() - start) > MIDI_TIMEOUT)
            return 0;
    }
    return 1;
}

/* 1 when it answers FEh */
int midi_command(uint8_t command) {
    uint32_t start;

    if (!midi_wait_ready())
        return 0;
    outp(midi_port + MIDI_STATUS, command);

    start = pit_now();
    while ((uint32_t)(pit_now() - start) < MIDI_ACK_TIMEOUT)
        if (!(inp(midi_port + MIDI_STATUS) & MIDI_DSR) &&
            inp(midi_port + MIDI_DATA) == MIDI_ACK)
            return 1;
    return 0;
}

int midi_init(uint16_t port) {
    midi_port         = port;
    midi_ready        = 0;
    midi_uart_unacked = 0;
    midi_lost_bytes   = 0;

    /* In UART mode the first reset isn't answered, the second is. Some
     * UART only interfaces never answer: an empty port reads FFh */
    if (!midi_command(MIDI_CMD_RESET) && !midi_command(MIDI_CMD_RESET)) {
        if (inp(midi_port + MIDI_STATUS) & MIDI_DRR)
            return 0;
        midi_uart_unacked = 1;
        midi_ready = 1;
        return 1;
    }

    if (!midi_command(MIDI_CMD_UART))
        midi_uart_unacked = 1;
    midi_ready = 1;
    return 1;
}

int midi_uart_unconfirmed() {
    return midi_uart_unacked;
}

int midi_send(uint8_t byte) {
    if (!midi_wait_ready()) {
        midi_lost_bytes++;
        return 0;
    }
    outp(midi_port + MIDI_DATA, byte);
    return 1;
}

/* Always with the status byte, same timing on every interface */
void midi_message(uint8_t status, uint8_t data1, uint8_t data2) {
    midi_send(status);
    midi_send(data1);
    midi_send(data2);
}

void midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
    midi_message(MIDI_NOTE_ON | channel, note, velocity);
}

/* Release velocity 64, the default */
void midi_note_off(uint8_t channel, uint8_t note) {
    midi_message(MIDI_NOTE_OFF | channel, note, 64);
}

void midi_control(uint8_t channel, uint8_t control, uint8_t value) {
    midi_message(MIDI_CONTROL | channel, control, value);
}

void midi_program(uint8_t channel, uint8_t program) {
    midi_send(MIDI_PROGRAM | channel);
    midi_send(program);
}

/* 2000h, no bend */
void midi_pitch_center(uint8_t channel) {
    midi_message(MIDI_BEND | channel, 0x00, 0x40);
}

void midi_sysex(uint8_t *data, unsigned length) {
    unsigned i;

    for (i = 0; i < length; i++)
        midi_send(data[i]);
}

unsigned midi_lost() {
    return midi_lost_bytes;
}

/* Sound off (120) is GM, the MT-32 only knows notes off (123) */
void midi_exit() {
    uint8_t channel;

    if (!midi_ready)
        return;
    for (channel = 0; channel < 16; channel++) {
        midi_control(channel, MIDI_CC_NOTES_OFF, 0);
        midi_control(channel, MIDI_CC_SOUND_OFF, 0);
    }
    /* Let the last bytes out */
    pit_delay_us(5000);
    midi_command(MIDI_CMD_RESET);
    midi_ready = 0;
}
