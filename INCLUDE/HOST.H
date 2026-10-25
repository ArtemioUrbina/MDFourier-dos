#ifndef HOST_H
#define HOST_H

/* What MDF runs under. In a Windows DOS box (enhanced mode) the VGA,
 * PIT, PIC and DMA are virtualized and the VM can be preempted.
 * The run goes on with looser timing, SB paths only and its own watermark */
#define HOST_DOS        0   /* plain DOS, also Win 3.x standard/real mode */
#define HOST_WINDOWS    1   /* Windows 3.x enhanced mode, 95, 98, ME */
#define HOST_NT         2   /* NT, 2000, XP: no hardware access */

int  host_detect();
int  host_is_windows();
/* 3 for Windows 3.x, 4 for 95/98/ME, 0 if its PIF hides the version */
int  host_windows_major();
char *host_name();

#endif
