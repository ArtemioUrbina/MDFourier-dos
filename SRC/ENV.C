#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "env.h"

int verbose = 0;
wss_cfg_t wss_cfg;

int parse_number(char *str, uint8_t base) {
    uint8_t  digit = 0;
    uint16_t value = 0;
    int digits = 0;
    int valid = 1;

    while(valid && *str) {
        if(*str >= '0' && *str <= '9')
            digit = *str - '0';
        else if(*str >= 'A' && *str <= 'F')
            digit = *str - 'A' + 10;
        else if(*str >= 'a' && *str <= 'f')
            digit = *str - 'a' + 10;
        else
            valid = 0;

        if(valid && digit < base) {
            value = value * base + digit;
            digits++;
            str++;
        } else
            valid = 0;
    }

    return digits ? value : -1;
}

int env_get_blaster(blaster_cfg_t *cfg) {
    char *blaster = NULL;

    cfg->port       = -1;
    cfg->irq        = -1;
    cfg->dma        = -1;
    cfg->high_dma   = -1;
    cfg->mpu_port   = -1;
    cfg->type       = -1;

    blaster = getenv("BLASTER");
    if(!blaster)
        return 0;

    while(*blaster) {
        while(*blaster == ' ')
            blaster++;
        if(*blaster) {
            switch(toupper((unsigned char)*blaster)) {
                case 'A':
                    cfg->port = parse_number(blaster + 1, 16);
                    break;
                case 'I':
                    cfg->irq = parse_number(blaster + 1, 10);
                    break;
                case 'D':
                    cfg->dma = parse_number(blaster + 1, 10);
                    break;
                case 'H':
                    cfg->high_dma = parse_number(blaster + 1, 10);
                    break;
                case 'P':
                    cfg->mpu_port = parse_number(blaster + 1, 16);
                    break;
                case 'T':
                    cfg->type = parse_number(blaster + 1, 10);
                    break;
                default:
                    // skip unknown characters
                    break;
            }
            blaster++;

            // skip numbers if parameter found
            while(*blaster && *blaster != ' ')
                blaster ++;
        }
    }
    
    return 1;
}

void print_env(blaster_cfg_t *cfg) {
    printf(" BLASTER: ");
    if(cfg->port != -1)
        printf("port=0x%03X ", cfg->port);
    if(cfg->irq != -1)
        printf("irq=%d ", cfg->irq);
    if(cfg->dma != -1)
        printf("dma=%d ", cfg->dma);
    if(cfg->high_dma != -1)
        printf("high_dma=%d ", cfg->high_dma);
    if(cfg->mpu_port != -1)
        printf("mpu_port=0x%03X ", cfg->mpu_port);
    if(cfg->type != -1)
        printf("type=%d ", cfg->type);
    printf("\n");
}

/* AUTO, OFF or port,irq,dma with the port in hex: 530,7,3 */
int env_parse_wss(char *arg) {
    char word[8];
    int  i, port;

    for (i = 0; i < 7 && arg[i] && arg[i] != ','; i++)
        word[i] = (char)toupper((unsigned char)arg[i]);
    word[i] = 0;

    if (!strcmp(word, "AUTO") && !arg[i]) {
        wss_cfg.mode = WSS_AUTO;
        return 1;
    }
    if (!strcmp(word, "OFF") && !arg[i]) {
        wss_cfg.mode = WSS_OFF;
        return 1;
    }
    /* parse_number stops at the comma, -1 without a digit */
    port = parse_number((char *)arg, 16);
    while (*arg && *arg != ',')
        arg++;
    if (*arg++ != ',')
        return 0;
    wss_cfg.irq = parse_number((char *)arg, 10);
    while (*arg && *arg != ',')
        arg++;
    if (*arg++ != ',')
        return 0;
    wss_cfg.dma = parse_number((char *)arg, 10);
    while (*arg >= '0' && *arg <= '9')
        arg++;

    if (port <= 0 || *arg || wss_cfg.irq < 2 || wss_cfg.irq > 15 ||
        wss_cfg.dma < 0 || wss_cfg.dma > 3 || wss_cfg.dma == 2)
        return 0;
    wss_cfg.port = (unsigned)port;
    if (wss_cfg.irq == 2)
        wss_cfg.irq = 9;
    wss_cfg.mode = WSS_MANUAL;
    return 1;
}
