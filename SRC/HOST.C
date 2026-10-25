/*
 * What MDF runs under: DOS, a Windows DOS box, or NT
 *
 * INT 2Fh AX=1600h answers the enhanced mode Windows version in AL/AH,
 * 00h or 80h when it isn't running (DOS, Windows 3.x standard or real
 * mode, Windows 9x "MS-DOS mode" we get real hardware access).
 * NT's NTVDM doesn't answer it, its DOS true version (INT 21h AX=3306h) is 5.50
 *
 * A PIF with "Prevent MS-DOS-based programs from detecting Windows"
 * makes 1600h answer 00h, but the DOS box still has windir in its environment,
 * MS-DOS mode and plain DOS don't
 */

#include <stdio.h>
#include <stdlib.h>
#include <i86.h>
#include "host.h"

int  host_kind = -1;
int  host_major;            /* Windows major version, 0 if hidden */
char host_text[48];

int host_detect() {
    union REGS reg;

    if (host_kind >= 0)
        return host_kind;

    host_kind = HOST_DOS;
    sprintf(host_text, "DOS");

    reg.x.ax = 0x3306;
    reg.x.bx = 0;
    int86(0x21, &reg, &reg);
    if (reg.x.bx == 0x3205) {
        host_kind = HOST_NT;
        sprintf(host_text, "Windows NT");
        return host_kind;
    }

    reg.x.ax = 0x1600;
    int86(0x2F, &reg, &reg); 
    if (reg.h.al == 0x00 || reg.h.al == 0x80) {
        if (getenv("windir")) {
            host_kind = HOST_WINDOWS;
            sprintf(host_text, "Windows (hidden by its PIF)");
        }
        return host_kind;
    }

    host_kind  = HOST_WINDOWS;
    host_major = reg.h.al;
    if (reg.h.al == 4 && reg.h.ah >= 90) 
        sprintf(host_text, "Windows ME (4.%02u)", reg.h.ah);
    else if (reg.h.al == 4 && reg.h.ah >= 10)
        sprintf(host_text, "Windows 98 (4.%02u)", reg.h.ah);
    else if (reg.h.al == 4)
        sprintf(host_text, "Windows 95 (4.%02u)", reg.h.ah);
    else
        sprintf(host_text, "Windows %u.%02u", reg.h.al, reg.h.ah);
    return host_kind;
}

int host_windows_major() {
    host_detect();
    return host_major;
}

int host_is_windows() {
    return host_detect() == HOST_WINDOWS;
}

char *host_name() {
    host_detect();
    return host_text;
}
