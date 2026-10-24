#ifndef WSS_H
#define WSS_H

#include <stdint.h>

/* Windows Sound System codec (AD1848/CS4231 compatible), as on the OPL3-SAx */

/* Indexed registers */
#define WSS_AUX2_L      0x04
#define WSS_AUX2_R      0x05
#define WSS_MODE_ID     0x0C

int     wss_present(unsigned port);
uint8_t wss_in(uint8_t reg);
void    wss_out(uint8_t reg, uint8_t val, uint8_t mce);

void    wss_use(unsigned port, uint8_t irq, uint8_t dma);
void    wss_select(unsigned port);
int     wss_in_use();

uint8_t wss_probe_irq(uint8_t dma);

#endif
