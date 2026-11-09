# MDFourier for DOS WIP

Currently working on a **16-bit DOS target build** for MDFourier. The decision was cut from 32 bit due to timing issues and aligning the pulses. It runs in real mode, no DOS extender needed. Every note starts on a VGA vertical retrace, so a VGA card is required.

Pure DOS is the reference. A Windows 3.x/9x DOS box also works, with looser timing and a watermark in the recording. Windows NT, 2000 and XP are refused, they have no access to the hardware.

It detects and plays using:

- **FM**: OPL2, OPL3 and OPL3 clones (OPL3-SAx, ESS ESFM)
- **PCM 8-bit mono**: any Sound Blaster DSP, 1.xx and up
- **ADPCM 4, 2.6 and 2-bit**: Creative DSPs 1.xx to 4.xx, in the CMS sequence; probed first, clones may not have it
- **PCM 8-bit stereo**: DSP 3.xx and 4.xx
- **PCM 16-bit stereo**: DSP 4.xx, ESS ES18xx in Extended mode, and the WSS codec of the OPL3-SAx (`/W`)
- **CMS**: Game Blaster, or the CMS chips on a Sound Blaster 1.x/2.0 (`/C`)

Where a card can't play a section, a filler takes its place so the sequence keeps the same timing on every card. The mixer is set for capture and restored at exit. 

On a Crystal CS4236/CS4237B codec (Orpheus II) the card turns SB FM 7 into +12 dB on the codec's wavetable port, where the Orpheus II's YMF289 comes in, and 0 dB on the Crystal FM. MDF sets the wavetable port to 0 dB right before playing and puts it back at exit. The card writes its SB levels back on every DSP command, so the FM sections play at 0 dB and the PCM sections leave it at the SB level. `/K` leaves it as it is.

## Sequences

| Sequence | Option | Contents | Length at 70 Hz |
|---|---|---|---:|
| FM and PCM | none | sync, chip ID, FM sweeps and steps, PCM mono, stereo and 16-bit | ~101 s |
| CMS | `/C` | sync from FM, or CMS on a Game Blaster; FM chip ID, sweep and levels; CMS notes, levels, voices, separation, noise, envelopes; ADPCM 4, 2.6 and 2-bit; PCM 8-bit mono | ~95 s |

## Usage

```
MDF [/C[:port]] [/K] [/R] [/T] [/V] [/W:AUTO | /W:OFF | /W:port,irq,dma]
```

- `/C` CMS sequence, for the SB 2.0 and Game Blaster, CMS at BLASTER A or the given port (`/C:220`). Runs on any card: a source it lacks gets a filler and its clock block stays silent, so MDFourier skips it
- `/K` keep the current mixer settings
- `/R` print the mixer settings and exit
- `/T` 30 s frame timing test without sound
- `/V` verbose: BLASTER, mixer, DSP and IRQ details. On a Crystal CS4236/CS4237B codec (Orpheus II and other cards with one) also the codec's own levels, as the card translated the SB mixer into them, and whether it is in SB or WSS mode
- `/W` 16-bit through a WSS codec: `AUTO`, `OFF` or set by hand

The `BLASTER` variable is needed for PCM and the mixer. Exit codes, for batch files:

| Code | Meaning |
|---:|---|
| 0 | complete, capture usable |
| 1 | bad command line |
| 2 | Windows NT, 2000 or XP |
| 3 | no usable vertical retrace |
| 4 | no FM at 0x388, under `/C` no CMS either |
| 5 | ESC pressed |
| 6 | a frame out of tolerance, discard the capture |
| 7 | WSS codec found, `/W` needed |
| 8 | the DSP raises another IRQ than BLASTER I |

## Current targets / audio cards

After reading https://nerdlypleasures.blogspot.com/2018/01/opl23-frequency-1hz-ish-difference.html I went into having MDfourier detect and classify the chip using a 6khz tone. The current MDfourier source code can ID and display this.

The values below were measured with MDFourier on real hardware, using `-j` for a **1/16 Hz FFT** with peak interpolation. The interpolation was validated against a synthetic **6002.30 Hz** tone.

| Card | FM Chip | Tone | Estimated Clock |
|---|---|---:|---:|
| Sound Blaster 2.0 CT1350B | OPL2 | 6002.3139 Hz | 49717.8503 Hz |
| Sound Blaster 16 CT1740 | OPL3 | 6003.2374 Hz | 49725.4998 Hz |
| Yamaha Audician 32 Plus | OPL3-SAx | 5978.3547 Hz | 49519.3937 Hz |
| Compaq ES1869 | ESS ESFM | 5991.0690 Hz | 49624.7075 Hz |

The Orpheus II (CS4237B) plays SB Pro stereo with left and right swapped against an SB16 and an OPL3-SAx, with or without the silent byte before the transfer. MDF records it as the card plays it.

Other cards it is being tested with: Sound Blaster 2.0 with CMS chips, ESS Solo-1, SB Live! SB0100, Montego II (Vortex 2), SB16 Vibra CT2960, Audigy 2 ZS and CMI8738.

## MDFourier

MDFourier's description and analysis tools are available here:

http://junkerhq.net/MDFourier

## Build / testing

Builds with the **Open Watcom compiler** and `wmake`, using the **16-bit tools**. The `MAKEFILE` uses the medium memory model: the code is over 64 KB with CMS.

Needs a 486SX or faster with the internal cache enabled. Developed and tested on machines ranging from a **486 DX2 through Pentium III**.

## AI assistance

Some of the code and documentation is written with the help of Claude, working from my design and direction. I am not an expert in DOS programming, but I have learned a lot over the past four months.

I take the time to read, review and understand every change, rewrite it where needed, and test it on real hardware. My goal is not speed, but understanding and results that are as correct as I can make them.