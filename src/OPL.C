#include <conio.h>
#include "opl.h"
#include "pit.h"

const uint16_t opl_port = 0x388;

void opl_write(uint8_t reg, uint8_t val) {
    outp(opl_port, reg);
    pit_delay_us(4);
    outp(opl_port + 1, val);
    pit_delay_us(23);
};

int opl_detect() {
    uint8_t status1, status2;

    opl_write(0x04, 0x60);
    opl_write(0x04, 0x80);
    status1 = (uint8_t)inp(opl_port);

    opl_write(0x02, 0xFF);
    opl_write(0x04, 0x21);
    pit_delay_us(90);
    status2 = (uint8_t)inp(opl_port);

    opl_write(0x04, 0x60);
    opl_write(0x04, 0x80);

    return(((status1 & 0xE0) == 0x00) && (status2 & 0xE0) == 0xC0);
}

void opl_reset() {
}

void opl_init() {
}

fm_driver_t opl_driver = {
    "OPL2/3 FM",
    opl_detect,
    opl_init,
    opl_write,
    opl_reset
};
