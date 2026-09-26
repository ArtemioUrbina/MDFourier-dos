/* 
 *  MDFourier for DOS
 *  Released under the GNU GPL
 */  

#include <stdio.h>
#include "carddrv.h"
#include "env.h"
#include "opl.h"
#include "pit.h"
#include "vsync.h"
#include "opltone.h"
#include "mdf.h"

#define SWEEP_FRAMES 20
#define SWEEP_STEPS  128

int main() {
    blaster_cfg_t cfg;

    printf("MDFourier DOS - Artemio Urbina 2026\n");
    if(!env_get_blaster(&cfg)) {
        printf("Could not read BLASTER environment variable\n");
        return 0;
    }
    print_env(&cfg);
    printf("Probing FM (%s)...", opl_driver.name);
    if(opl_driver.detect()) {
        printf("found (%s)\n", opl_is_opl3() ? "OPL3" : "OPL2");
        opl_driver.init();

        printf("Playing MDFourier...\n");
        mdf_run_full_test(0, SWEEP_FRAMES, SWEEP_STEPS);

    } else
        printf("not found\n");

    return 0;
}
