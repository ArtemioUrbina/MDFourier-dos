# MDFourier for DOS WIP

Currently working on a **16-bit DOS target build** for MDFourier. The decision was cut form 32 bit due to timing issues and aligning the pulses to 32 bits. It will refuse running under Windows, so run under pure DOS. Memory extenders are not an issue.

Right now it detects and supports **FM** hardware, including:

- OPL2
- OPL3
- OPL3 clones

The intention is to also support **CMS and PCM variants**.

## Current targets / audio cards

After reading https://nerdlypleasures.blogspot.com/2018/01/opl23-frequency-1hz-ish-difference.html I went into having MDfourier detect and classify the chip using a 6khz tone. The current MDfourier source code can ID and display this.

The values below were measured with MDFourier on real hardware, using `-j` for a **1/16 Hz FFT** with peak interpolation. The interpolation was validated against a synthetic **6002.30 Hz** tone.

| Card | FM Chip | Tone | Estimated Clock |
|---|---|---:|---:|
| Sound Blaster 2.0 CT1350B | OPL2 | 6002.3139 Hz | 49717.8503 Hz |
| Sound Blaster 16 CT1740 | OPL3 | 6003.2374 Hz | 49725.4998 Hz |
| Yamaha Audician 32 Plus | OPL3-SAx | 5978.3547 Hz | 49519.3937 Hz |
| Compaq ES1869 | ESS ESFM | 5991.0690 Hz | 49624.7075 Hz |

## MDFourier

MDFourier's description and analysis tools are available here:

http://junkerhq.net/MDFourier

## Build / testing

Builds with the **Watcom compiler** and `wmake`.

It currently needs the **16-bit tools**.

Developed and tested on machines ranging from a **486 DX2 through Pentium III**.
