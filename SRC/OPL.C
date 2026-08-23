#include <conio.h>
#include "opl.h"
#include "pit.h"

const uint16_t opl_port = 0x388;

void opl_write(uint8_t reg, uint8_t val) {
    outp(opl_port, reg);
    /* index write settle time >= 3.3us */
    pit_delay_us(4);
    outp(opl_port + 1, val);
    /* data write settle time >= 23us */
    pit_delay_us(23);
};

/*
 * AdLib timer test: start timer 1, wait for it
 * and check the status register flags
 */
int opl_detect() {
    uint8_t status1, status2;

    /* select timer control reg, mask both timers */
    opl_write(0x04, 0x60);
    /* reset IRQ */
    opl_write(0x04, 0x80);
    status1 = (uint8_t)inp(opl_port);

    /* timer 1 data register */
    opl_write(0x02, 0xFF);
    /* unmask + start timer 1 */
    opl_write(0x04, 0x21);
    /* let timer 1 expire, 80us and margin */
    pit_delay_us(90);
    status2 = (uint8_t)inp(opl_port);

    /* cleanup */
    opl_write(0x04, 0x60);
    opl_write(0x04, 0x80);

    return(((status1 & 0xE0) == 0x00) && (status2 & 0xE0) == 0xC0);
}

void opl_init() {
    uint8_t reg;

    /* max attenuation on every operator */
    for (reg = 0x40; reg <= 0x55; reg++)
        opl_write(reg, 0x3F);
    /* key off on every channel */
    for (reg = 0xB0; reg <= 0xB8; reg++)
        opl_write(reg, 0x00);
}

void opl_reset() {
    opl_init();
}

fm_driver_t opl_driver = {
    "OPL2/3 FM",
    opl_detect,
    opl_init,
    opl_write,
    opl_reset
};
