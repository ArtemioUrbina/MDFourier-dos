/*
 * Crystal CS4236 family codecs (CS4236, CS4237B and compatibles)
 *
 * The CS4237B has a Sound Blaster Pro interface (DSP 3.02) and a WSS
 * codec sharing its IRQ and DMA. The card switches between the two by
 * itself as they are accessed. In SB mode the SB Pro mixer levels are
 * translated into the codec's own registers, so the levels that reach
 * the output are only known by reading the codec.
 *
 * Unlike the AD1848 layout WSS.C uses (index at base+4), the codec's
 * registers start right at WSSbase, usually 534h:
 *   R0 WSSbase+0  index: bit 7 INIT (busy), bit 6 MCE, bits 4-0 I0-I31
 *   R1 WSSbase+1  indexed data
 * The extended registers X0-X25 are reached through I23, in MODE 3 only:
 * 17h to R0, the X address with XRAE set to R1, then R1 is the X data.
 * Writing R0 again turns I23 back into the address register.
 *
 * We find it by its IDs: I12 bits 3-0 read 1010b, I25 (MODE 2 or 3)
 * reads chip ID 00011b for the CS4236 family, X25 (MODE 3) tells the
 * CS4237B and its revision. When the codec is not in MODE 3 we raise it
 * for the X registers, and put the mode back.
 *
 * /V prints its levels and SRS 3D. Unless /K is given, SRS 3D is turned
 * off with the mixer setup (crystal_srs_off()) and the wavetable port,
 * where the Orpheus II's YMF289 comes in, is set to 0 dB right before
 * playing (crystal_synth_unity()), the rest is left as the card set it.
 *
 * The Global Status register at CTRLbase+7 tells the context, bit 7
 * set for WSS and clear for SB. CTRLbase is its own Plug and Play
 * device; we try WSSbase+4, the usual place on these cards, and accept
 * it only when its C1 register reads the same chip ID as X25.
 *
 * Source: Cirrus Logic CS4237B data sheet DS213PP4
 */

#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <i86.h>
#include "crystal.h"
#include "env.h"
#include "mixer.h"
#include "pit.h"
#include "wss.h"
#include "carddrv.h"

#define CRYSTAL_R0          0
#define CRYSTAL_R1          1
#define CRYSTAL_INIT        0x80
#define CRYSTAL_INDEX_BITS  0x7F    /* MCE, TRD and the index, what we put back in R0 */

#define CRYSTAL_I12         0x0C    /* MODE and ID: bit 7 reads 1, CMS1,0 bits 6-5, ID bits 3-0 */
#define CRYSTAL_I23         0x17    /* extended register access */
#define CRYSTAL_I25         0x19    /* compatibility ID */

#define CRYSTAL_ID_MASK     0x8F
#define CRYSTAL_ID          0x8A    /* bit 7 and 1010b: CS4231 and later */
#define CRYSTAL_CMS_MASK    0x60
#define CRYSTAL_CMS_MODE2   0x40
#define CRYSTAL_CMS_MODE3   0x60
#define CRYSTAL_CID_MASK    0x1F
#define CRYSTAL_CID_CS4236  0x03    /* I25 for the CS4236 and CS4237B */
#define CRYSTAL_CID_CS4237B 0x08    /* X25, revision C and later. Revision B reads 00h */
#define CRYSTAL_XRAE        0x08

#define CRYSTAL_X_COUNT     26
#define CRYSTAL_X4          4       /* bit 4 IFM, bit 3 WTRMD, bit 2 FMRM */
#define CRYSTAL_X25         25
#define CRYSTAL_IFM         0x10
#define CRYSTAL_WTRMD       0x08
#define CRYSTAL_FMRM        0x04

#define CRYSTAL_MUTE        0x80    /* bit 7 of every level register: output mute */

#define CRYSTAL_CTRL_OFFSET 4       /* usual CTRLbase, WSSbase+4 */
#define CRYSTAL_CTRL_INDEX  3
#define CRYSTAL_CTRL_DATA   4
#define CRYSTAL_CTRL_STATUS 7       /* Global Status, bit 7 CWSS */
#define CRYSTAL_C1          1       /* chip version and ID, same as X25 */
#define CRYSTAL_C2          2       /* SRS space bits 7-4, center bits 3-0, 1.5 dB steps */
#define CRYSTAL_C3          3       /* bit 7 3DEN, bit 6 3DM, bit 5 3DSO, the rest reserved */
#define CRYSTAL_3DEN        0x80    /* SRS 3D on */
#define CRYSTAL_3DM         0x40    /* SRS mono-to-stereo on */
#define CRYSTAL_3DSO        0x20    /* serial output from the SRS DSP, not heard */
#define CRYSTAL_CWSS        0x80

#define CRYSTAL_SB_MASTER   0x22    /* SB Pro mixer master, read to touch the SB side */

/* Waits on the codec busy, 20 ms at most */
#define CRYSTAL_TIMEOUT     PIT_US_TO_CLK(20000UL)

/* The usual WSS bases with the codec at +4, as Plug and Play sets them */
unsigned crystal_ports[] = { 0x534, 0x608, 0xE84, 0xF44 };
#define CRYSTAL_PORT_COUNT  (sizeof(crystal_ports) / sizeof(crystal_ports[0]))

uint16_t crystal_base;
uint8_t  crystal_ireg[32];
uint8_t  crystal_xreg[CRYSTAL_X_COUNT];

int crystal_wait() {
    uint32_t start = pit_now();

    while (inp(crystal_base + CRYSTAL_R0) & CRYSTAL_INIT)
        if ((uint32_t)(pit_now() - start) > CRYSTAL_TIMEOUT)
            return 0;

    return 1;
}

uint8_t crystal_in(uint8_t reg) {
    crystal_wait();
    outp(crystal_base + CRYSTAL_R0, reg);
    return (uint8_t)inp(crystal_base + CRYSTAL_R1);
}

void crystal_out(uint8_t reg, uint8_t val) {
    crystal_wait();
    outp(crystal_base + CRYSTAL_R0, reg);
    outp(crystal_base + CRYSTAL_R1, val);
}

/* X0-X15 by XA3-XA0 in bits 7-4, X16 and up need XA4 in bit 2 */
uint8_t crystal_x_in(uint8_t reg) {
    uint8_t address = (uint8_t)(((reg & 0x0F) << 4) | ((reg & 0x10) >> 2) | CRYSTAL_XRAE);
    uint8_t val;

    crystal_wait();
    _disable();
    outp(crystal_base + CRYSTAL_R0, CRYSTAL_I23);
    outp(crystal_base + CRYSTAL_R1, address);
    val = (uint8_t)inp(crystal_base + CRYSTAL_R1);
    /* R0 written again: I23 back to the address register */
    outp(crystal_base + CRYSTAL_R0, CRYSTAL_I23);
    _enable();
    return val;
}

/* ---- Levels, from the data sheet tables ---- */

/* Table 10, AUX1, AUX2, LINE: 5 bits, +12 dB at 0, 01000b = 0 dB, 1.5 dB steps */
double crystal_db_line(uint8_t val) {
    return 12.0 - 1.5 * (val & 0x1F);
}

/* Table 6, PC wave and FM attenuation: 6 bits, 0 dB at 0, 1.5 dB steps */
double crystal_db_attenuation(uint8_t val) {
    return -1.5 * (val & 0x3F);
}

/* Table 6, wavetable serial port: 6 bits, +12 dB at 0, 001000b = 0 dB */
double crystal_db_wavetable(uint8_t val) {
    return 12.0 - 1.5 * (val & 0x3F);
}

/* Table 12, master digital gain: 7 bits, 0 dB at 0 down to -94.5 dB at
 * 3Fh, 40h-47h are +12 to +1.5 dB, 48h and up reserved (reported as 99) */
double crystal_db_master(uint8_t val) {
    val &= 0x7F;
    if (val < 0x40)
        return -1.5 * val;
    if (val < 0x48)
        return 12.0 - 1.5 * (val - 0x40);
    return 99.0;
}

char *crystal_mute_text(uint8_t left, uint8_t right) {
    if ((left & CRYSTAL_MUTE) && (right & CRYSTAL_MUTE))
        return ", muted";
    if (left & CRYSTAL_MUTE)
        return ", left muted";
    if (right & CRYSTAL_MUTE)
        return ", right muted";
    return "";
}

void crystal_print_pair(char *name, char *regs, uint8_t left, uint8_t right,
                        double (*to_db)(uint8_t)) {
    printf("        %-15s %-8s %+5.1f/%+5.1f dB%s (%02Xh %02Xh)\n", name, regs,
           to_db(left), to_db(right), crystal_mute_text(left, right), left, right);
}

char *crystal_chip_name(uint8_t x25) {
    switch (x25 >> 5) {
        case 0: return (x25 & CRYSTAL_CID_MASK) == CRYSTAL_CID_CS4237B ? "CS4237B" :
                       "CS4236 family, X25 not implemented (CS4236 or CS4237B rev B)";
        case 6: return (x25 & CRYSTAL_CID_MASK) == CRYSTAL_CID_CS4237B ? "CS4237B rev C/D" :
                       "CS4236 family";
        case 7: return (x25 & CRYSTAL_CID_MASK) == CRYSTAL_CID_CS4237B ? "CS4237B rev E" :
                       "CS4236 family";
    }
    return "CS4236 family";
}

int crystal_mode_number(uint8_t i12) {
    if ((i12 & CRYSTAL_CMS_MASK) == CRYSTAL_CMS_MODE3)
        return 3;
    if (i12 & CRYSTAL_CMS_MODE2)
        return 2;
    return 1;
}

/* ---- Context, from the Control device ---- */

/* CTRLbase+7 as read before the codec was touched */
uint8_t crystal_status_first;

/* Control indirect registers C0-C8: the index at CTRLbase+3, the data at
 * +4. The index is put back as it was */
uint8_t crystal_ctrl_in(uint16_t ctrl, uint8_t reg) {
    uint8_t index = (uint8_t)(inp(ctrl + CRYSTAL_CTRL_INDEX) & 0x0F);
    uint8_t val;

    outp(ctrl + CRYSTAL_CTRL_INDEX, reg);
    val = (uint8_t)inp(ctrl + CRYSTAL_CTRL_DATA);
    outp(ctrl + CRYSTAL_CTRL_INDEX, index);
    return val;
}

void crystal_ctrl_out(uint16_t ctrl, uint8_t reg, uint8_t val) {
    uint8_t index = (uint8_t)(inp(ctrl + CRYSTAL_CTRL_INDEX) & 0x0F);

    outp(ctrl + CRYSTAL_CTRL_INDEX, reg);
    outp(ctrl + CRYSTAL_CTRL_DATA, val);
    outp(ctrl + CRYSTAL_CTRL_INDEX, index);
}

/* The Control device at WSSbase+4, if its C1 reads the chip ID X25 has */
int crystal_ctrl_found() {
    return crystal_ctrl_in((uint16_t)(crystal_base + CRYSTAL_CTRL_OFFSET), CRYSTAL_C1) ==
           crystal_xreg[CRYSTAL_X25];
}

/* SRS 3D: on/off, mono-to-stereo, and its space and center gains, from
 * 0 dB down in 1.5 dB steps */
void crystal_print_srs(uint16_t ctrl) {
    uint8_t c2 = crystal_ctrl_in(ctrl, CRYSTAL_C2);
    uint8_t c3 = crystal_ctrl_in(ctrl, CRYSTAL_C3);

    printf(" Codec: SRS 3D %s%s, space %+.1f dB, center %+.1f dB (C2 %02Xh C3 %02Xh)\n",
           (c3 & CRYSTAL_3DEN) ? "on" : "off", (c3 & CRYSTAL_3DM) ? ", mono-to-stereo on" : "",
           -1.5 * (c2 >> 4), -1.5 * (c2 & 0x0F), c2, c3);
}

char *crystal_context(uint8_t status) {
    return (status & CRYSTAL_CWSS) ? "WSS" : "SB";
}

/* Whether reading the codec moved the card to WSS, and an SB access back */
void crystal_report_context(uint16_t sb_base) {
    uint16_t ctrl = (uint16_t)(crystal_base + CRYSTAL_CTRL_OFFSET);
    uint8_t  after;

    if (!crystal_xreg[CRYSTAL_X25]) {
        printf(" Codec: context unknown, X25 reads 0, no ID to find the Control device by\n");
        return;
    }
    if (!crystal_ctrl_found()) {
        printf(" Codec: context unknown, no Control device at 0x%03X\n", ctrl);
        return;
    }

    after = (uint8_t)inp(ctrl + CRYSTAL_CTRL_STATUS);
    printf(" Codec: context %s, %s after reading the codec (Control at 0x%03X)",
           crystal_context(crystal_status_first), crystal_context(after), ctrl);
    if ((after & CRYSTAL_CWSS) && !(crystal_status_first & CRYSTAL_CWSS) && sb_base) {
        mix_read(sb_base, CRYSTAL_SB_MASTER);
        printf(",\n        %s after an SB mixer read", crystal_context((uint8_t)inp(ctrl + CRYSTAL_CTRL_STATUS)));
    }
    printf("\n");
    crystal_print_srs(ctrl);
}

/* ---- Report ---- */

void crystal_print_levels() {
    uint8_t x4  = crystal_xreg[CRYSTAL_X4];
    int     ifm = (x4 & CRYSTAL_IFM) != 0;
    int     fm_remap = ifm && (x4 & CRYSTAL_FMRM);
    int     i;

    printf("        X4 %02Xh: internal FM %s, FM remap %s, wavetable remap %s\n", x4,
           ifm ? "on" : "off", fm_remap ? "to X6/X7" : "off",
           (x4 & CRYSTAL_WTRMD) ? "off" : "if WTEN");

    /* With internal FM on, I6/I7 are the PC wave level and X14/X15 the master */
    if (ifm) {
        crystal_print_pair("PC wave", "I6/I7", crystal_ireg[6], crystal_ireg[7], crystal_db_attenuation);
        crystal_print_pair("master digital", "X14/X15", crystal_xreg[14], crystal_xreg[15], crystal_db_master);
    } else {
        crystal_print_pair("DAC master", "I6/I7", crystal_ireg[6], crystal_ireg[7], crystal_db_master);
        crystal_print_pair("(unused)", "X14/X15", crystal_xreg[14], crystal_xreg[15], crystal_db_master);
    }

    if (fm_remap) {
        crystal_print_pair("LINE (synth in)", "X0/X1", crystal_xreg[0], crystal_xreg[1], crystal_db_line);
        printf("        %-15s %-8s %02Xh %02Xh, written through to X6/X7\n", "synth remap", "I18/I19",
               crystal_ireg[18], crystal_ireg[19]);
    } else {
        crystal_print_pair("LINE (synth in)", "I18/I19", crystal_ireg[18], crystal_ireg[19], crystal_db_line);
        crystal_print_pair("LINE alternate", "X0/X1", crystal_xreg[0], crystal_xreg[1], crystal_db_line);
    }
    crystal_print_pair("Crystal FM", "X6/X7", crystal_xreg[6], crystal_xreg[7], crystal_db_attenuation);
    crystal_print_pair("wavetable port", "X16/X17", crystal_xreg[16], crystal_xreg[17], crystal_db_wavetable);
    crystal_print_pair("AUX1", "I2/I3", crystal_ireg[2], crystal_ireg[3], crystal_db_line);
    crystal_print_pair("AUX2 (CD)", "I4/I5", crystal_ireg[4], crystal_ireg[5], crystal_db_line);

    printf("        I0-I31:");
    for (i = 0; i < 32; i++)
        printf("%s%02X", i == 16 ? "\n               " : " ", crystal_ireg[i]);
    printf("\n        X0-X17:");
    for (i = 0; i < 18; i++)
        printf(" %02X", crystal_xreg[i]);
    printf(", X25 %02X\n", crystal_xreg[CRYSTAL_X25]);
}

/* All the I registers, and the X registers in MODE 3 */
void crystal_read_all() {
    uint8_t reg;

    for (reg = 0; reg < 32; reg++)
        crystal_ireg[reg] = crystal_in(reg);
    for (reg = 0; reg < CRYSTAL_X_COUNT; reg++)
        crystal_xreg[reg] = (reg <= 17 || reg == CRYSTAL_X25) ? crystal_x_in(reg) : 0;
}

/* ---- Access ---- */

uint8_t  crystal_saved_r0, crystal_saved_i12;

/* A CS4236 family codec at port: I12 and I25 IDs. When it is one, it is
 * left in MODE 3 for crystal_close() to put back. Otherwise R0 and the
 * mode are already back */
int crystal_open(unsigned port, int report) {
    uint8_t cid;

    crystal_base = (uint16_t)port;
    crystal_saved_r0 = (uint8_t)inp(crystal_base + CRYSTAL_R0);
    if (crystal_saved_r0 == 0xFF || !crystal_wait())
        return 0;

    crystal_saved_i12 = crystal_in(CRYSTAL_I12);
    if ((crystal_saved_i12 & CRYSTAL_ID_MASK) != CRYSTAL_ID) {
        if (report)
            printf(" Codec: none at 0x%03X, I12 reads %02Xh\n", port, crystal_saved_i12);
        outp(crystal_base + CRYSTAL_R0, (uint8_t)(crystal_saved_r0 & CRYSTAL_INDEX_BITS));
        return 0;
    }

    /* I25 needs MODE 2 */
    if (crystal_mode_number(crystal_saved_i12) < 2)
        crystal_out(CRYSTAL_I12, CRYSTAL_CMS_MODE2);
    cid = crystal_in(CRYSTAL_I25);

    if ((cid & CRYSTAL_CID_MASK) != CRYSTAL_CID_CS4236) {
        if (report)
            printf(" Codec: CS4231 compatible at 0x%03X, not Crystal (I25 %02Xh)\n", port, cid);
        crystal_out(CRYSTAL_I12, (uint8_t)(crystal_saved_i12 & CRYSTAL_CMS_MASK));
        outp(crystal_base + CRYSTAL_R0, (uint8_t)(crystal_saved_r0 & CRYSTAL_INDEX_BITS));
        return 0;
    }

    if (crystal_mode_number(crystal_saved_i12) < 3)
        crystal_out(CRYSTAL_I12, CRYSTAL_CMS_MODE3);
    return 1;
}

void crystal_close() {
    crystal_out(CRYSTAL_I12, (uint8_t)(crystal_saved_i12 & CRYSTAL_CMS_MASK));
    outp(crystal_base + CRYSTAL_R0, (uint8_t)(crystal_saved_r0 & CRYSTAL_INDEX_BITS));
}

/* Where the codec is, looked for once. 0 if none */
unsigned crystal_port;
int      crystal_searched;
int      crystal_searched_report;   /* the search said why each port wasn't one */

unsigned crystal_find(int report) {
    unsigned i;

    /* A quiet search found none: again, to say why */
    if (crystal_searched && (crystal_port || crystal_searched_report || !report))
        return crystal_port;

    crystal_searched = 1;
    crystal_searched_report = report;
    for (i = 0; i < CRYSTAL_PORT_COUNT; i++) {
        /* The context before the codec is touched */
        crystal_status_first = (uint8_t)inp(crystal_ports[i] + CRYSTAL_CTRL_OFFSET + CRYSTAL_CTRL_STATUS);
        if (crystal_open(crystal_ports[i], report)) {
            crystal_close();
            crystal_port = crystal_ports[i];
            break;
        }
    }

    return crystal_port;
}

int crystal_is_cs4237b() {
    uint8_t x25;

    if (!crystal_find(0) || !crystal_open(crystal_port, 0))
        return 0;
    x25 = crystal_x_in(CRYSTAL_X25);
    crystal_close();
    return x25 && (x25 & CRYSTAL_CID_MASK) == CRYSTAL_CID_CS4237B;
}

/* ---- Report ---- */

unsigned crystal_check(uint16_t sb_base) {
    /* Found earlier: the context now, before we touch it again */
    if (crystal_searched && crystal_port)
        crystal_status_first = (uint8_t)inp(crystal_port + CRYSTAL_CTRL_OFFSET + CRYSTAL_CTRL_STATUS);
    if (!crystal_find(1))
        return 0;

    if (!crystal_open(crystal_port, 1))
        return 0;
    crystal_read_all();
    crystal_close();

    printf(" Codec: Crystal %s (X25 %02Xh) at 0x%03X, MODE %d%s\n",
           crystal_chip_name(crystal_xreg[CRYSTAL_X25]), crystal_xreg[CRYSTAL_X25], crystal_port,
           crystal_mode_number(crystal_saved_i12),
           crystal_mode_number(crystal_saved_i12) < 3 ? " (MODE 3 to read)" : "");
    crystal_print_levels();
    crystal_report_context(sb_base);
    return crystal_port;
}

/* ---- FM chip at 0 dB ---- */

/* The card writes the SB FM level as one value to the LINE (X0/X1),
 * Crystal FM (X6/X7) and wavetable port (X16/X17) registers. Measured
 * on an Orpheus II:
 *   SB FM      7     6     5     4      3      2      1    0
 *   value      00h   02h   05h   07h    0Bh    0Eh    12h  mute
 *   LINE, WT   +12   +9    +4.5  +1.5   -4.5   -9     -15  dB
 *   FM         0     -3    -7.5  -10.5  -16.5  -21    -27  dB
 * At FM 7 the Crystal FM is at 0 dB and the wavetable port, where the
 * Orpheus' YMF289 comes in, at +12 dB. No SB level gives it 0 dB, so
 * we write its gain to 0 dB, mute bit as it is, and put it back at exit.
 *
 * The card writes its SB levels into the codec again on every DSP
 * command (reset, speaker, time constant), not on SB mixer or FM
 * writes. So we write it right before playing, after the last setup
 * command: the FM sections hold it, the PCM sections put the SB level
 * back after them. Apart from the 16-bit section, which plays through
 * it, the codec is left alone during the sequence: any access moves the
 * card to WSS mode */
#define CRYSTAL_X16         16
#define CRYSTAL_X17         17
#define CRYSTAL_WT_KEEP     0xC0    /* mute and the reserved bit */
#define CRYSTAL_WT_GAIN     0x3F
#define CRYSTAL_WT_0DB      0x08

int     crystal_synth_set;
uint8_t crystal_synth_saved_l, crystal_synth_saved_r;

void crystal_x_out(uint8_t reg, uint8_t val) {
    uint8_t address = (uint8_t)(((reg & 0x0F) << 4) | ((reg & 0x10) >> 2) | CRYSTAL_XRAE);

    crystal_wait();
    _disable();
    outp(crystal_base + CRYSTAL_R0, CRYSTAL_I23);
    outp(crystal_base + CRYSTAL_R1, address);
    outp(crystal_base + CRYSTAL_R1, val);
    outp(crystal_base + CRYSTAL_R0, CRYSTAL_I23);
    _enable();
}

void crystal_synth_restore() {
    if (!crystal_synth_set || !crystal_open(crystal_port, 0))
        return;
    crystal_x_out(CRYSTAL_X16, crystal_synth_saved_l);
    crystal_x_out(CRYSTAL_X17, crystal_synth_saved_r);
    crystal_close();
    crystal_synth_set = 0;
}

void crystal_synth_unity(uint16_t sb_base) {
    uint8_t x4, left, right;

    if (!crystal_find(verbose) || !crystal_open(crystal_port, 0))
        return;

    /* X16/X17 follow the SB FM level only with wavetable remap on */
    x4 = crystal_x_in(CRYSTAL_X4);
    if (x4 & CRYSTAL_WTRMD) {
        crystal_close();
        return;
    }

    crystal_synth_saved_l = crystal_x_in(CRYSTAL_X16);
    crystal_synth_saved_r = crystal_x_in(CRYSTAL_X17);
    crystal_x_out(CRYSTAL_X16, (uint8_t)((crystal_synth_saved_l & CRYSTAL_WT_KEEP) | CRYSTAL_WT_0DB));
    crystal_x_out(CRYSTAL_X17, (uint8_t)((crystal_synth_saved_r & CRYSTAL_WT_KEEP) | CRYSTAL_WT_0DB));
    crystal_close();
    crystal_synth_set = 1;
    atexit(crystal_synth_restore);

    /* Back to SB mode to see whether the card kept it, and to play in */
    mix_read(sb_base, CRYSTAL_SB_MASTER);
    left = right = 0;
    if (crystal_open(crystal_port, 0)) {
        left  = crystal_x_in(CRYSTAL_X16);
        right = crystal_x_in(CRYSTAL_X17);
        crystal_close();
    }
    mix_read(sb_base, CRYSTAL_SB_MASTER);

    if ((left & CRYSTAL_WT_GAIN) == CRYSTAL_WT_0DB && (right & CRYSTAL_WT_GAIN) == CRYSTAL_WT_0DB)
        printf(" Codec: wavetable port (FM chip) %+.1f/%+.1f dB, set to 0 dB\n",
               crystal_db_wavetable(crystal_synth_saved_l), crystal_db_wavetable(crystal_synth_saved_r));
    else
        printf(" Codec: wavetable port written to 0 dB, the card set it back to %+.1f/%+.1f dB\n",
               crystal_db_wavetable(left), crystal_db_wavetable(right));
}

/* ---- SRS 3D off for capture ---- */

/* SRS 3D (C3 3DEN, and 3DM for mono-to-stereo) processes the output: it
 * spreads one-channel sounds into both channels, lifts them and the
 * highs, and raises the noise floor (measured on an Orpheus II). It sits
 * in the Control device, not behind the SB mixer. Unless /K, we turn it
 * off with the mixer setup and put it back at exit, as with the ESS and
 * OPL3-SAx 3D */
uint16_t crystal_ctrl;
int      crystal_ctrl_searched;
int      crystal_srs_saved;
uint8_t  crystal_srs_c3;

/* The Control device, accepted when its C1 reads the chip ID X25 has. 0 if none */
uint16_t crystal_find_ctrl() {
    uint16_t ctrl;
    uint8_t  x25;

    if (crystal_ctrl_searched)
        return crystal_ctrl;
    crystal_ctrl_searched = 1;

    if (!crystal_find(verbose) || !crystal_open(crystal_port, 0))
        return 0;
    x25 = crystal_x_in(CRYSTAL_X25);
    crystal_close();

    ctrl = (uint16_t)(crystal_port + CRYSTAL_CTRL_OFFSET);
    if (x25 && crystal_ctrl_in(ctrl, CRYSTAL_C1) == x25)
        crystal_ctrl = ctrl;
    return crystal_ctrl;
}

void crystal_srs_restore() {
    if (!crystal_srs_saved)
        return;
    crystal_ctrl_out(crystal_ctrl, CRYSTAL_C3,
                     (uint8_t)(crystal_srs_c3 & (CRYSTAL_3DEN | CRYSTAL_3DM | CRYSTAL_3DSO)));
    crystal_srs_saved = 0;
}

void crystal_srs_off() {
    uint8_t c3;

    if (!crystal_find_ctrl())
        return;
    c3 = crystal_ctrl_in(crystal_ctrl, CRYSTAL_C3);
    if (!(c3 & (CRYSTAL_3DEN | CRYSTAL_3DM)))
        return;

    crystal_srs_c3    = c3;
    crystal_srs_saved = 1;
    atexit(crystal_srs_restore);
    crystal_ctrl_out(crystal_ctrl, CRYSTAL_C3, (uint8_t)(c3 & CRYSTAL_3DSO));
    printf(" Codec: SRS 3D off for capture, back on at exit\n");
}

int crystal_srs_on() {
    if (!crystal_find_ctrl())
        return 0;
    return (crystal_ctrl_in(crystal_ctrl, CRYSTAL_C3) & (CRYSTAL_3DEN | CRYSTAL_3DM)) != 0;
}

/* ---- The card ---- */

/* The codec plays 16-bit as a WSS codec, its registers 4 above the WSS
 * base WSS.C works from, on the IRQ and DMA it shares with the SB side:
 * BLASTER I and D. /W:OFF leaves it out, /W:port,irq,dma at that base
 * gives them by hand */
#define CRYSTAL_WSS_OFFSET  4

void crystal_detect(blaster_cfg_t *cfg) {
    unsigned codec = crystal_find(verbose);
    unsigned base;

    if (!codec || wss_in_use() || env_wss_cfg.mode == WSS_OFF)
        return;

    base = codec - CRYSTAL_WSS_OFFSET;
    if (env_wss_cfg.mode == WSS_MANUAL) {
        if (env_wss_cfg.port != base)
            return;
        wss_use(base, (uint8_t)env_wss_cfg.irq, (uint8_t)env_wss_cfg.dma);
    } else {
        if (cfg->irq < 2 || cfg->dma < 0)
            return;
        wss_use(base, (uint8_t)(cfg->irq == 2 ? 9 : cfg->irq), (uint8_t)cfg->dma);
    }

    if (verbose)
        printf(" PCM 16-bit: Crystal codec at 0x%03X, IRQ and DMA shared with the SB side\n", codec);
}

/* A CS4237B keeps the SB Pro output filter bit, with no filter behind
 * it on the Orpheus II: the mixer doesn't warn about it. Measured on
 * that chip only, other Crystal chips warn */
void crystal_mixer_notes() {
    if (crystal_is_cs4237b())
        mixer_sbpro_filter_unused();
}

void crystal_report(uint16_t sb_base) {
    crystal_check(sb_base);
}

card_t crystal_card = {
    "Crystal CS4236/CS4237B",
    crystal_detect,
    NULL,               /* needs_config: it plays 16-bit without /W */
    crystal_mixer_notes,
    NULL,               /* mixer_set: the card maps the SB mixer itself */
    crystal_srs_off,
    NULL,               /* mixer_report: crystal_report() shows its levels */
    crystal_srs_on,
    "Crystal SRS 3D on, alters the output",
    crystal_report,
    crystal_synth_unity /* the FM chip's wavetable port to 0 dB */
};
