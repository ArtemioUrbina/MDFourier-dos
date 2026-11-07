#include <stdio.h>
#include <conio.h>
#include "cms.h"
#include "dsp.h"
#include "pit.h"

/*
 * Creative Music System / Game Blaster, two Philips SAA1099
 *
 * Frequency: clock / 512 * 2^octave / (511 - freq), 27.5 Hz (octave 0,
 * freq 3) to ~7 kHz. Registers per chip:
 *   00-05  amplitude, right in bits 4-7, left in 0-3
 *   08-0D  frequency
 *   10-12  octave, two voices each: even voice bits 0-2, odd 4-6
 *   14     tone enable, bit per voice
 *   15     noise enable, bit per voice
 *   16     noise generators: voices 0-2 bits 0-1, 3-5 bits 4-5
 *   18-19  envelope generators (SAA1099 datasheet, not in Creative's):
 *          bit 7 on, 5 external clock, 4 3-bit resolution, 3-1 shape,
 *          0 right inverted. Internal clock: frequency generator 1 or 4
 *   1C     bit 1 resets and synchronizes the generators, bit 0 sound on
 */

#define REG_AMPL        0x00
#define REG_FREQ        0x08
#define REG_OCTAVE      0x10
#define REG_TONE_EN     0x14
#define REG_NOISE_EN    0x15
#define REG_NOISE_GEN   0x16
#define REG_ENVELOPE    0x18
#define REG_CONTROL     0x1C

#define CTRL_SOUND_ON   0x01
#define CTRL_SYNC       0x02

/* Game Blaster detection latch: written at base+6, read back at base+A.
 * On a Sound Blaster those are the DSP's reset and read ports */
#define LATCH_WRITE     6
#define LATCH_READ      0x0A

uint16_t cms_base = 0x220;

/* Write only chips, what's set in the shared registers */
uint8_t  cms_tone_enables[2];
uint8_t  cms_noise_enables[2];
uint8_t  cms_octaves[2][3];
uint8_t  cms_noise_rates[2];

char *cms_detect_reason = "not tried";

void cms_write(int chip, uint8_t reg, uint8_t val) {
    uint16_t port = (uint16_t)(cms_base + chip * 2);

    outp(port + 1, reg);
    outp(port, val);
}

int cms_latch_answers(uint8_t val) {
    outp(cms_base + LATCH_WRITE, val);
    return (uint8_t)inp(cms_base + LATCH_READ) == val;
}

int cms_detect(uint16_t base) {
    dsp_version_t ver;

    cms_base = base;

    /* A DSP here is a Sound Blaster. Only 1.x and 2.0 have the sockets */
    if (dsp_reset(base)) {
        if (!dsp_get_version(base, &ver)) {
            cms_detect_reason = "a DSP answers without a version";
            return CMS_NONE;
        }
        if (ver.major >= 3) {
            cms_detect_reason = "DSP 3.xx and later cards have no CMS sockets";
            return CMS_NONE;
        }
        cms_detect_reason = "on a Sound Blaster 1.x/2.0, the chips can't be detected";
        return CMS_ON_SB;
    }

    if (cms_latch_answers(0xC6) && cms_latch_answers(0x39)) {
        cms_detect_reason = "Game Blaster detection latch";
        return CMS_LATCH;
    }
    cms_detect_reason = "no DSP and no Game Blaster latch";
    return CMS_NONE;
}

char *cms_detect_text() {
    return cms_detect_reason;
}

void cms_clear_chip(int chip) {
    uint8_t reg;

    for (reg = 0; reg < 0x20; reg++)
        cms_write(chip, reg, 0);
    cms_tone_enables[chip]   = 0;
    cms_noise_enables[chip]  = 0;
    cms_octaves[chip][0] = cms_octaves[chip][1] = cms_octaves[chip][2] = 0;
    cms_noise_rates[chip] = 0;
}

void cms_reset() {
    int chip;

    for (chip = 0; chip < 2; chip++) {
        cms_clear_chip(chip);
        cms_write(chip, REG_CONTROL, CTRL_SYNC);
        cms_write(chip, REG_CONTROL, CTRL_SOUND_ON);
    }
}

void cms_silence() {
    cms_clear_chip(0);
    cms_clear_chip(1);
}

void cms_set_octave(int chip, int ch, uint8_t octave) {
    uint8_t *reg = &cms_octaves[chip][ch >> 1];

    if (ch & 1)
        *reg = (uint8_t)((*reg & 0x07) | ((octave & 7) << 4));
    else
        *reg = (uint8_t)((*reg & 0x70) | (octave & 7));
    cms_write(chip, (uint8_t)(REG_OCTAVE + (ch >> 1)), *reg);
}

void cms_set_enables(int chip, int ch, int tone, int noise) {
    uint8_t bit = (uint8_t)(1 << ch);

    cms_tone_enables[chip]  = (uint8_t)(tone  ? cms_tone_enables[chip]  | bit : cms_tone_enables[chip]  & ~bit);
    cms_noise_enables[chip] = (uint8_t)(noise ? cms_noise_enables[chip] | bit : cms_noise_enables[chip] & ~bit);
    cms_write(chip, REG_TONE_EN,  cms_tone_enables[chip]);
    cms_write(chip, REG_NOISE_EN, cms_noise_enables[chip]);
}

void cms_tone(int voice, uint8_t octave, uint8_t freq, uint8_t left, uint8_t right) {
    int chip = voice / 6, ch = voice % 6;

    cms_write(chip, (uint8_t)(REG_FREQ + ch), freq);
    cms_set_octave(chip, ch, octave);
    cms_write(chip, (uint8_t)(REG_AMPL + ch), (uint8_t)(((right & 15) << 4) | (left & 15)));
    cms_set_enables(chip, ch, 1, 0);
}

void cms_noise(int voice, int rate, uint8_t left, uint8_t right) {
    int chip = voice / 6, ch = voice % 6;
    int shift = ch < 3 ? 0 : 4;

    cms_noise_rates[chip] = (uint8_t)((cms_noise_rates[chip] & ~(3 << shift)) | ((rate & 3) << shift));
    cms_write(chip, REG_NOISE_GEN, cms_noise_rates[chip]);
    cms_write(chip, (uint8_t)(REG_AMPL + ch), (uint8_t)(((right & 15) << 4) | (left & 15)));
    cms_set_enables(chip, ch, 0, 1);
}

/* Amplitude only: the generator keeps running, the note starts at once */
void cms_level(int voice, uint8_t left, uint8_t right) {
    cms_write(voice / 6, (uint8_t)(REG_AMPL + voice % 6), (uint8_t)(((right & 15) << 4) | (left & 15)));
}

/* Both chip generators restarted together. Same notes in phase */
void cms_sync() {
    int chip;

    for (chip = 0; chip < 2; chip++) {
        cms_write(chip, REG_CONTROL, CTRL_SYNC | CTRL_SOUND_ON);
        cms_write(chip, REG_CONTROL, CTRL_SOUND_ON);
    }
}

/* Every amplitude to 0, generators and enables kept */
void cms_mute_all() {
    int chip, ch;

    for (chip = 0; chip < 2; chip++)
        for (ch = 0; ch < 6; ch++)
            cms_write(chip, (uint8_t)(REG_AMPL + ch), 0);
}

void cms_envelope(int chip, int gen, uint8_t ctrl) {
    cms_write(chip, (uint8_t)(REG_ENVELOPE + gen), ctrl);
}
