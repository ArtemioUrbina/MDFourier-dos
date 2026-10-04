#include <conio.h>
#include "opl.h"
#include "pit.h"

const uint16_t opl_port = 0x388;

static int opl3_found = 0;

static void opl_write_port(uint16_t port, uint8_t reg, uint8_t val)
{
    outp(port, reg);
    /* index write settle time >= 3.3us */
    pit_delay_us(4);
    outp(port + 1, val);
    /* data write settle time >= 23us */
    pit_delay_us(23);
}

void opl_write(uint8_t reg, uint8_t val)
{
    opl_write_port(opl_port, reg, val);
}

/*
 * OPL3 second register bank (0x100-0x1FF) at base+2 / base+3.
 * Only call on OPL3
 */
static void opl_write_bank1(uint8_t reg, uint8_t val)
{
    opl_write_port((uint16_t)(opl_port + 2), reg, val);
}

int opl_is_opl3(void)
{
    return opl3_found;
}

void opl3_set_new_mode(int on)
{
    if (opl3_found)
        opl_write_bank1(0x05, on ? 0x01 : 0x00);
}

/*
 * AdLib timer test: start timer 1, wait for it
 * and check the status register flags
 */
int opl_detect() {
    uint8_t status1, status2;
    int found;

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

    found = ((status1 & 0xE0) == 0x00) && ((status2 & 0xE0) == 0xC0);

    /* OPL3 reads status bits 1-2 as 0, OPL2 reads them as 1 */
    opl3_found = found && ((inp(opl_port) & 0x06) == 0x00);

    return found;
}

/*
 * Put the chip in a known state
 */
void opl_init() {
    uint8_t reg;

    /* max attenuation, fastest release, key off */
    for (reg = 0x40; reg <= 0x55; reg++)
        opl_write(reg, 0x3F);
    for (reg = 0x80; reg <= 0x95; reg++)
        opl_write(reg, 0x0F);           /* SL=0, RR=15 */
    for (reg = 0xB0; reg <= 0xB8; reg++)
        opl_write(reg, 0x00);           /* key off */

    if (opl3_found) {
        for (reg = 0x40; reg <= 0x55; reg++)
            opl_write_bank1(reg, 0x3F);
        for (reg = 0x80; reg <= 0x95; reg++)
            opl_write_bank1(reg, 0x0F);
        for (reg = 0xB0; reg <= 0xB8; reg++)
            opl_write_bank1(reg, 0x00);
        opl_write_bank1(0x04, 0x00);
        opl_write_bank1(0x05, 0x00);
    }

    /* Let  envelope finish its (now RR=15) release. */
    pit_delay_us(20000);

    /* Everything else to a known value. 0x40 (TL=max) and 0x80 */
    opl_write(0x01, 0x20);              /* WSE on, test bits off */
    opl_write(0x02, 0x00);
    opl_write(0x03, 0x00);
    opl_write(0x04, 0x60);              /* timers masked + stopped */
    opl_write(0x04, 0x80);              /* reset IRQ flags */
    opl_write(0x08, 0x00);              /* CSM off, NTS=0 */
    for (reg = 0x20; reg <= 0x35; reg++)
        opl_write(reg, 0x00);
    for (reg = 0x60; reg <= 0x75; reg++)
        opl_write(reg, 0x00);
    for (reg = 0xA0; reg <= 0xA8; reg++)
        opl_write(reg, 0x00);
    opl_write(0xBD, 0x00);              /* rhythm off, AM/VIB depth off */
    for (reg = 0xC0; reg <= 0xC8; reg++)
        opl_write(reg, 0x00);
    for (reg = 0xE0; reg <= 0xF5; reg++)
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
