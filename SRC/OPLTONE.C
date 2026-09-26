/*
 * OPL note/instrument layer, built on opl_write().
 */

#include "opltone.h"
#include "opl.h"

/*
 * Channel/operator register offsets for the 9 two-op channels.
 * These are register offsets, not operator indices: the offset space
 * has gaps at 0x06-0x07 and 0x0E-0x0F.
 */
static const uint8_t op_mod[9] = { 0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12 };
static const uint8_t op_car[9] = { 0x03, 0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x13, 0x14, 0x15 };

#define OPL_TEST_WSE_REG 0x01
#define OPL_TEST_WSE_BIT 0x20

#define OPL_KEY_ON       0x20

/* Last Block/Fnum-high value written to 0xB0+ch, without the key-on
 * bit. Key-off rewrites this instead of 0x00, so the release tail
 * keeps playing at the note's pitch. */
static uint8_t b0_shadow[9];

void opl_freq_to_block_fnum(unsigned freq_hz, uint8_t *block, uint16_t *fnum)
{
    uint8_t  b;
    uint64_t f = 0;

    if (freq_hz > OPL_MAX_FREQ_HZ)
        freq_hz = OPL_MAX_FREQ_HZ;
    if (freq_hz == 0)
        freq_hz = 1;

    /* freq_hz << 20 can exceed 32 bits at the
     * low end of the block search (b=0)  */
    for (b = 0; b <= 7; b++) {
        f = ((uint64_t)freq_hz << (20 - b)) / 49716ULL;
        if (f <= 1023ULL)
            break;
    }
    if (b > 7)
        b = 7;

    *block = b;
    *fnum  = (uint16_t)f;
}

static uint8_t make_b0(uint8_t block, uint16_t fnum)
{
    return (uint8_t)(((block & 0x07) << 2) | ((fnum >> 8) & 0x03));
}

void opl_note_set(uint8_t channel, uint8_t block, uint16_t fnum)
{
    if (channel > 8)
        return;

    b0_shadow[channel] = make_b0(block, fnum);
    opl_write((uint8_t)(0xA0 + channel), (uint8_t)(fnum & 0xFF));
    opl_write((uint8_t)(0xB0 + channel), b0_shadow[channel]);
}

void opl_key_on(uint8_t channel)
{
    if (channel > 8)
        return;

    opl_write((uint8_t)(0xB0 + channel),
              (uint8_t)(OPL_KEY_ON | b0_shadow[channel]));
}

void opl_note_on_raw(uint8_t channel, uint8_t block, uint16_t fnum)
{
    if (channel > 8)
        return;

    b0_shadow[channel] = make_b0(block, fnum);
    opl_write((uint8_t)(0xA0 + channel), (uint8_t)(fnum & 0xFF));
    opl_write((uint8_t)(0xB0 + channel),
              (uint8_t)(OPL_KEY_ON | b0_shadow[channel]));
}

void opl_note_on(uint8_t channel, unsigned freq_hz)
{
    uint8_t  block;
    uint16_t fnum;

    opl_freq_to_block_fnum(freq_hz, &block, &fnum);
    opl_note_on_raw(channel, block, fnum);
}

void opl_note_off(uint8_t channel)
{
    if (channel > 8)
        return;

    opl_write((uint8_t)(0xB0 + channel), b0_shadow[channel]);
}

void opl_set_instrument(uint8_t channel, opl_instrument_t instrument)
{
    uint8_t mod, car;

    if (channel > 8)
        return;     /* only the 9 two-op channels are mapped above */

    mod = op_mod[channel];
    car = op_car[channel];

    /* Wave Select Enable */
    opl_write(OPL_TEST_WSE_REG, OPL_TEST_WSE_BIT);

    /* Carrier: shared attack/decay/sustain-level for both
     * instruments */
    opl_write((uint8_t)(0x20 + car), 0x21); /* sustain-enable, multiple=1 */
    opl_write((uint8_t)(0x40 + car), 0x00); /* KSL=0, TL=0 (max volume) */
    opl_write((uint8_t)(0x60 + car), 0xF0); /* attack=15, decay=0 */
    opl_write((uint8_t)(0xE0 + car), 0x00); /* waveform=sine */

    if (instrument == OPL_INSTRUMENT_SINE) {
        /* Release=15 (fastest) */
        opl_write((uint8_t)(0x80 + car), 0x0F); /* sustain level=0, release=15 */

        /* Modulator fully silent */
        opl_write((uint8_t)(0x20 + mod), 0x21);
        opl_write((uint8_t)(0x40 + mod), 0x3F); /* TL=max attenuation */
        opl_write((uint8_t)(0x60 + mod), 0x00); /* attack=0: never sounds */
        opl_write((uint8_t)(0x80 + mod), 0x0F);
        opl_write((uint8_t)(0xE0 + mod), 0x00);
        opl_write((uint8_t)(0xC0 + channel), 0x00); /* FM, feedback=0 */
    } else {
        opl_write((uint8_t)(0x80 + car), 0x08); /* sustain level=0, release=8 */

        /* Modulator a mid level, with decay */
        opl_write((uint8_t)(0x20 + mod), 0x21);
        opl_write((uint8_t)(0x40 + mod), 0x08); /* TL=8, present but not loud */
        opl_write((uint8_t)(0x60 + mod), 0xF4); /* attack=15, decay=4 */
        opl_write((uint8_t)(0x80 + mod), 0x48); /* sustain=4, release=8 */
        opl_write((uint8_t)(0xE0 + mod), 0x00);
        opl_write((uint8_t)(0xC0 + channel), 0x06); /* FM, feedback=3 */
    }
}
