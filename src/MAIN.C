/* 
 *  MDFourier for DOS
 *  Released under the GNU GPL
 */  

#include <stdio.h>
#include "carddrv.h"
#include "env.h"
#include "opl.h"
#include "pit.h"

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
        printf("found\n");
    } else
        printf("not found\n");
    return 0;
}
