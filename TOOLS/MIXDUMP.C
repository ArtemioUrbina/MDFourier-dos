/*
 * Print Sound Blaster mixer register
 * Port from BLASTER (A220 if not set).
 * build with wcl -bt=dos -ms -0 mixdump.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <conio.h>

unsigned blaster_port(void) {
    char *s = getenv("BLASTER");

    while (s && *s) {
        if (*s == 'A' || *s == 'a')
            return (unsigned)strtoul(s + 1, NULL, 16);
        s++;
    }
    return 0x220;
}

int main(void) {
    unsigned base = blaster_port(), reg = 0;

    printf("Mixer registers at 0x%03X (index 0x%03X, data 0x%03X)\n",
           base, base + 4, base + 5);
    printf("     ");
    for (reg = 0; reg < 16; reg++)  
        printf(" %X ", reg);
    for (reg = 0; reg < 0x80; reg++) {
        if (!(reg & 15))
            printf("\n%02X: ", reg);
        outp(base + 4, reg);
        printf(" %02X", inp(base + 5));
    }
    printf("\n");
    return 0;
}
