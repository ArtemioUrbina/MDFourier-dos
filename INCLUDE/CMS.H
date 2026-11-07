#ifndef CMS_H
#define CMS_H

#include <stdint.h>

/* Creative Music System / Game Blaster: two Philips SAA1099, six voices
 * each. Voices 0-5 on the first chip (data base+0, address base+1),
 * 6-11 on the second (base+2, base+3). Write only */
#define CMS_VOICES      12
#define CMS_CLOCK_HZ    7159090.0       /* twice the NTSC colour burst */

#define CMS_NONE        0
#define CMS_LATCH       1   /* Game Blaster, its detection latch answered */
#define CMS_ON_SB       2   /* SB 1.x/2.0 sockets, the chips can't be read */

#define CMS_NOISE_HIGH  0   /* clock / 256, 28 kHz */
#define CMS_NOISE_MID   1   /* clock / 512, 14 kHz */
#define CMS_NOISE_LOW   2   /* clock / 1024, 7 kHz */
#define CMS_NOISE_TONE  3   /* from tone generator 0 or 3 of its chip */

int     cms_detect(uint16_t base);
char   *cms_detect_text();

void    cms_reset();
void    cms_silence();

void    cms_write(int chip, uint8_t reg, uint8_t val);

void    cms_tone(int voice, uint8_t octave, uint8_t freq, uint8_t left, uint8_t right);
void    cms_noise(int voice, int rate, uint8_t left, uint8_t right);
void    cms_level(int voice, uint8_t left, uint8_t right);
void    cms_mute_all();
void    cms_sync();

#define CMS_ENV_ON      0x80
#define CMS_ENV_3BIT    0x10
#define CMS_ENV_SHAPE(n) ((uint8_t)((n) << 1))
void    cms_envelope(int chip, int gen, uint8_t ctrl);

#endif
