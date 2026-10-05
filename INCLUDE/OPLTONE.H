#ifndef OPLTONE_H
#define OPLTONE_H

#include <stdint.h>

/*
 * (49716Hz is the OPL2/3 internal sample rate: 3.579545MHz / 72)
 * Source:
 * (Nerdly Pleasures OPL2/3 frequency writeup, ModdingWiki OPL_chip
 * page, Cosmodoc AdLib Functions reference).
 */

#define OPL_MAX_FREQ_HZ 6209u

typedef enum {
    OPL_INSTRUMENT_DEFAULT,
    OPL_INSTRUMENT_SINE
} opl_instrument_t;

void opl_set_instrument(uint8_t channel, opl_instrument_t instrument);

void opl_freq_to_block_fnum(unsigned freq_hz, uint8_t *block, uint16_t *fnum);

void opl_note_on(uint8_t channel, unsigned freq_hz);

void opl_note_on_raw(uint8_t channel, uint8_t block, uint16_t fnum);

void opl_note_set(uint8_t channel, uint8_t block, uint16_t fnum);
void opl_key_on(uint8_t channel);
void opl_note_off(uint8_t channel);

#define OPL_PAN_LEFT    0x10
#define OPL_PAN_RIGHT   0x20
#define OPL_PAN_CENTER  (OPL_PAN_LEFT | OPL_PAN_RIGHT)

void opl_set_pan(uint8_t channel, uint8_t pan);
void opl_set_carrier_mult(uint8_t channel, uint8_t mult);


#endif
