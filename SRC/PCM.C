/*
 * 8-bit mono PCM through the Sound Blaster DSP.
 *
 * Sources:
 * https://pdos.csail.mit.edu/6.828/2017/readings/hardware/SoundBlaster.pdf
 * Intel 8237A data sheet (DMA), Intel 8259A data sheet (PIC)
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "pcm.h"
#include "dsp.h"

#define DSP_READ_READY  0x0E    /* reading it acks the 8-bit IRQ */

#define DSP_SPEAKER_ON  0xD1
#define DSP_SPEAKER_OFF 0xD3
#define DSP_TIME_CONST  0x40
#define DSP_PLAY_8BIT   0x14    /* single-cycle, 8-bit mono */
#define DSP_BLOCK_SIZE  0x48    /* auto-init block size, DSP 2.00+ */
#define DSP_AUTO_8BIT   0x1C    /* auto-init, 8-bit mono, DSP 2.00+ */
#define DSP_EXIT_AUTO   0xDA    /* stop auto-init at the end of the block */

/* Rate = 1000000 / (256 - TC). TC 211 gives 22222.2 Hz, in range for
 * normal mode on every DSP version */
#define PCM_TC          211
#define PCM_RATE        (1000000.0 / 45.0)

/* Tones fill this share of their block, the slowest card measured plays 2.2% long */
#define FILL_PCT        85

/* Clock tone: rate / 8 (2777.8 Hz), below any card's output filter */
#define CLK_PERIOD      8
#define CLK_MAX         5600
#define CLK_BLOCKS      4
#define CLK_PCT         95

/* Step tones */
#define SWEEP_LOW       20.0
#define SWEEP_HIGH      10000.0
#define LEVEL_FREQ      (PCM_RATE / 22.0)   /* 1010.1 Hz */
#define FADE            32                  /* samples, in and out */

/* Sweep and clock 3 dB below full scale, so the first Level step
 * (full scale, 1 kHz) is always the loudest PCM element. MDFourier
 * normalizes to the loudest one, it must not depend on each card's
 * bass or treble response */
#define SWEEP_AMP       181                 /* of 256, -3.0 dB */
#define CLK_AMP         90.0                /* of 127, -3.0 dB */
#define STEP_MAX        5600                /* 20 frames at 85% */

/* Samples built per frame. A 20-frame step needs 8 frames, a 10-frame
 * one 4: a few ms per frame, even without an FPU */
#define FILL_CHUNK      768

/* Sine table, 1024 entries, +-127 */
#define SINE_BITS       10
#define SINE_SIZE       (1 << SINE_BITS)
int8_t sine[SINE_SIZE];

/* DMA buffers: clock, then two steps. All inside one 64 KB page */
#define AREA_NEED       (CLK_MAX + 2u * STEP_MAX)
uint8_t __far dma_area[AREA_NEED * 2u];
uint8_t __far *clk_buf;
uint8_t __far *step_buf[2];
uint16_t step_len[2];

/* 8237 ports, channels 0-3 */
#define DMA_MASK        0x0A
#define DMA_MODE        0x0B
#define DMA_FLIPFLOP    0x0C
#define DMA_MODE_SINGLE 0x48    /* single, increment, memory to card */
#define DMA_MODE_AUTO   0x58    /* same, auto-initialize */
const uint8_t dma_page_port[4] = { 0x87, 0x83, 0x81, 0x82 };

/* 8259 ports */
#define PIC1_CMD        0x20
#define PIC1_DATA       0x21
#define PIC2_CMD        0xA0
#define PIC2_DATA       0xA1
#define PIC_EOI         0x20

int       ready;
uint16_t  base;
uint8_t   irq, dma;
uint8_t   dsp_major;
double    frame_rate;

void (__interrupt __far *old_vector)();
uint8_t   old_pic1, old_pic2;
int       installed;

volatile unsigned irq_count;

/* DSP 1.xx has no auto-init. The ISR restarts single-cycle transfers
 * over the auto-init DMA until clk_blocks have played */
volatile int      clk_restart;
unsigned          clk_blocks;
uint16_t          clk_len;
int               clk_exit_sent;
int               playing;       /* a transfer may be running */

/* Results */
typedef struct {
    const char *name;
    unsigned    expected;
    unsigned    got;
} pcm_count_t;

pcm_count_t count_clock = { "clock", 0, 0 };
pcm_count_t count_steps[2] = { { "sweep", 0, 0 }, { "level", 0, 0 } }; 

unsigned step_irq_base;
int      step_pending;

unsigned vector_of(uint8_t n) {
    return n < 8 ? 0x08 + n : 0x70 + (n - 8);
}

void __interrupt __far pcm_isr() {
    inp(base + DSP_READ_READY);
    irq_count++;
    if (clk_restart && irq_count < clk_blocks) {
        uint16_t count = (uint16_t)(clk_len - 1);

        dsp_write(base, DSP_PLAY_8BIT);	
        dsp_write(base, (uint8_t)(count & 0xFF));
        dsp_write(base, (uint8_t)(count >> 8));
    }
    if (irq >= 8)
        outp(PIC2_CMD, PIC_EOI);
    outp(PIC1_CMD, PIC_EOI);
}

uint32_t phys_of(void __far *p) {
    return ((uint32_t)FP_SEG(p) << 4) + FP_OFF(p);
}

/* The half of dma_area that doesn't cross a 64 KB page */
void pick_buffers() {
    uint8_t __far *area = dma_area;
    uint32_t start = phys_of(dma_area);

    if ((start >> 16) != ((start + AREA_NEED - 1) >> 16))
        area = dma_area + AREA_NEED;
    clk_buf     = area;
    step_buf[0] = area + CLK_MAX;
    step_buf[1] = area + CLK_MAX + STEP_MAX;
}

void build_tables() {
    unsigned i;

    for (i = 0; i < SINE_SIZE; i++)
        sine[i] = (int8_t)floor(127.0 * sin(2.0 * 3.14159265358979 * i / SINE_SIZE) + 0.5);

    /* sin at rate / 8 starting at 45 degrees: the period's last
     * sample is 0x80 and the next one carries on the same sine */
    for (i = 0; i < CLK_MAX; i++)
        clk_buf[i] = (uint8_t)(128 + floor(CLK_AMP *
                     sin(2.0 * 3.14159265358979 * ((i % CLK_PERIOD) + 1) / CLK_PERIOD) + 0.5));
}

void dma_program(uint8_t __far *buf, uint16_t len, uint8_t mode) {
    uint32_t phys  = phys_of(buf);
    uint16_t addr  = (uint16_t)(phys & 0xFFFF);
    uint16_t count = (uint16_t)(len - 1);

    outp(DMA_MASK, 0x04 | dma);
    outp(DMA_FLIPFLOP, 0);
    outp(DMA_MODE, mode | dma);
    outp(dma * 2, addr & 0xFF);
    outp(dma * 2, addr >> 8);
    outp(dma * 2 + 1, count & 0xFF);
    outp(dma * 2 + 1, count >> 8);
    outp(dma_page_port[dma], (uint8_t)(phys >> 16));
    outp(DMA_MASK, dma);
}

void dma_mask() {
    outp(DMA_MASK, 0x04 | dma);
}

int dsp_play(uint8_t cmd, uint16_t len) {
    uint16_t count = (uint16_t)(len - 1);

    return dsp_write(base, cmd) &&
           dsp_write(base, (uint8_t)(count & 0xFF)) &&
           dsp_write(base, (uint8_t)(count >> 8));
}

/* A transfer left running: stop it and get the DSP back. The rate
 * goes before the speaker on, that one can drop what follows it */
void recover() {
    dma_mask();
    clk_restart = 0;
    dsp_reset(base);
    dsp_write(base, DSP_TIME_CONST);
    dsp_write(base, PCM_TC);
    dsp_write(base, DSP_SPEAKER_ON);
    playing = 0;
}

void pcm_uninstall() {
    if (!installed)
        return;
    _disable();
    if (irq >= 8)
        outp(PIC2_DATA, old_pic2);
    outp(PIC1_DATA, old_pic1);
    _dos_setvect(vector_of(irq), old_vector);
    _enable();
    installed = 0;
}

void pcm_install() {
    _disable();
    old_vector = _dos_getvect(vector_of(irq));
    _dos_setvect(vector_of(irq), pcm_isr);
    old_pic1 = (uint8_t)inp(PIC1_DATA);
    if (irq >= 8) {
        old_pic2 = (uint8_t)inp(PIC2_DATA);
        outp(PIC2_DATA, old_pic2 & ~(1 << (irq - 8)));
        outp(PIC1_DATA, old_pic1 & ~(1 << 2));      /* cascade */
    } else
        outp(PIC1_DATA, old_pic1 & ~(1 << irq));
    _enable();
    installed = 1;
    atexit(pcm_uninstall);
}

int pcm_init(blaster_cfg_t *cfg, double frame_hz) {
    dsp_version_t ver;

    ready = 0;
    if (cfg->port == -1 || cfg->irq == -1 || cfg->dma == -1) {
        printf("PCM: BLASTER needs A, I and D\n");
        return 0;
    }
    if (cfg->dma > 3 || cfg->irq < 2 || cfg->irq > 15) {
        printf("PCM: unsupported I%d or D%d\n", cfg->irq, cfg->dma);
        return 0;
    }
    base = (uint16_t)cfg->port;
    irq  = (uint8_t)(cfg->irq == 2 ? 9 : cfg->irq);   /* IRQ 2 is 9 on AT */
    dma  = (uint8_t)cfg->dma;
    frame_rate = frame_hz;

    if (!dsp_reset(base) || !dsp_get_version(base, &ver)) {
        printf("PCM: DSP not answering\n");
        return 0;
    }
    dsp_major = ver.major;

    pick_buffers();
    build_tables();
    pcm_install();
    ready = 1;

    printf("PCM: 8-bit mono, %.1f Hz, IRQ %u, DMA %u, clock %s\n",
           PCM_RATE, irq, dma, dsp_major >= 2 ? "auto-init" : "restarted (DSP 1.xx)");
    return 1;
}

void pcm_prepare() {
    if (!ready)
        return;
    dsp_write(base, DSP_SPEAKER_ON);
}

/* ---- Clock tone ---- */

void pcm_clock_arm(unsigned frames) {
    unsigned long want;

    if (!ready)
        return;

    /* Whole cosine periods per DMA block */
    want = (unsigned long)(PCM_RATE * frames * CLK_PCT / 100.0 / frame_rate);
    clk_blocks = CLK_BLOCKS;
    want = want / clk_blocks;
    want -= want % CLK_PERIOD;
    if (want > CLK_MAX)
        want = CLK_MAX - CLK_MAX % CLK_PERIOD;
    clk_len = (uint16_t)want;
    clk_exit_sent = 0;
    count_clock.expected = clk_blocks;

    /* Rate set here, frames after the speaker on */
    dsp_write(base, DSP_TIME_CONST);
    dsp_write(base, PCM_TC);
    dma_program(clk_buf, clk_len, DMA_MODE_AUTO);
    if (dsp_major >= 2)
        dsp_play(DSP_BLOCK_SIZE, clk_len);
}

void pcm_clock_start() {
    if (!ready)
        return;

    irq_count = 0;
    if (dsp_major >= 2) {
        /* One block only: exit right away, it stops at its end */
        if (dsp_write(base, DSP_AUTO_8BIT))
            playing = 1;
        if (clk_blocks == 1) {
            dsp_write(base, DSP_EXIT_AUTO);
            clk_exit_sent = 1;
        }
    } else {
        clk_restart = 1;
        if (dsp_play(DSP_PLAY_8BIT, clk_len))
            playing = 1;
    }
}

void pcm_clock_frame() {
    if (!ready || !playing || dsp_major < 2 || clk_exit_sent)
        return;
    if (irq_count >= clk_blocks - 1) {
        dsp_write(base, DSP_EXIT_AUTO);
        clk_exit_sent = 1;
    }
}

void pcm_clock_stop() {
    if (!ready)
        return;

    count_clock.got = irq_count;
    clk_restart = 0;
    if (irq_count >= clk_blocks) {
        dma_mask();
        playing = 0;
    } else
        recover();
}

/* ---- Steps ---- */

double step_freq(pcm_steps_t kind, unsigned step) {
    if (kind == PCM_LEVEL)
        return LEVEL_FREQ;
    return SWEEP_LOW * pow(SWEEP_HIGH / SWEEP_LOW, (double)step / (PCM_SWEEP_STEPS - 1));
}

struct {
    int            active;
    unsigned       buf;
    uint8_t __far *out;
    unsigned       len, pos, amp;
    uint32_t       phase, inc;
} fill;

void fill_samples(unsigned max) {
    unsigned end, i;

    if (!fill.active)
        return;
    end = fill.pos + max;
    if (end > fill.len)
        end = fill.len;

    for (i = fill.pos; i < end; i++) {
        /* +-127 * 256 fits an int, halve towards zero, symmetric */
        int s = sine[(uint16_t)(fill.phase >> 16) >> (16 - SINE_BITS)] * (int)fill.amp;

        s = (s + (s >= 0 ? 128 : -128)) / 256;
        if (i < FADE)
            s = s * (int)i / FADE;
        else if (i >= fill.len - FADE)
            s = s * (int)(fill.len - 1 - i) / FADE;
        fill.out[i] = (uint8_t)(128 + s);
        fill.phase += fill.inc;
    }
    fill.pos = end;
    if (fill.pos >= fill.len)
        fill.active = 0;
}

void pcm_step_fill(pcm_steps_t kind, unsigned step, unsigned buf, unsigned frames) {
    unsigned len;

    if (!ready)
        return;

    len = (unsigned)(PCM_RATE * frames * FILL_PCT / 100.0 / frame_rate);
    if (len > STEP_MAX)
        len = STEP_MAX;
    step_len[buf] = (uint16_t)len;

    fill.buf    = buf;
    fill.out    = step_buf[buf];
    fill.len    = len;
    fill.pos    = 0;
    fill.phase  = 0;
    fill.inc    = (uint32_t)(step_freq(kind, step) * 4294967296.0 / PCM_RATE);
    fill.amp    = kind == PCM_LEVEL ? 256u >> step : SWEEP_AMP;   /* 6 dB per step */
    fill.active = 1;
}

void pcm_fill_more() {
    fill_samples(FILL_CHUNK);
}

void pcm_step_start(pcm_steps_t kind, unsigned buf) {
    pcm_count_t *c = &count_steps[kind];

    if (!ready)
        return;

    if (step_pending) {
        if (irq_count != step_irq_base)
            c->got++;
        else
            recover();
    }

    /* Should be done by now, the rest if not */
    if (fill.active && fill.buf == buf)
        fill_samples(STEP_MAX);

    step_irq_base = irq_count;
    dma_program(step_buf[buf], step_len[buf], DMA_MODE_SINGLE);
    c->expected++;
    step_pending = dsp_play(DSP_PLAY_8BIT, step_len[buf]);
    playing = step_pending;
}

void pcm_steps_end(pcm_steps_t kind) {
    pcm_count_t *c = &count_steps[kind];

    if (!ready || !step_pending)
        return;
    if (irq_count != step_irq_base) {
        c->got++;
        dma_mask();
        playing = 0;
    } else
        recover();
    step_pending = 0;
}

void pcm_finish() {
    if (!ready)
        return;

    dma_mask();
    if (playing)
        dsp_reset(base);
    dsp_write(base, DSP_SPEAKER_OFF);
    playing = 0;
}

int print_count(pcm_count_t *c) {
    if (!c->expected)
        return 1;
    printf(" %s %u/%u", c->name, c->got, c->expected);
    return c->got == c->expected;
}

void pcm_print_report() {
    int ok;

    if (!ready)
        return;
    printf("  PCM IRQs:");
    ok  = print_count(&count_clock);
    ok &= print_count(&count_steps[PCM_SWEEP]);
    ok &= print_count(&count_steps[PCM_LEVEL]);
    printf("\n");
    if (!ok)
        printf("  WARNING: PCM IRQ %u missing, check BLASTER I and the card's jumpers\n", irq);
}
