#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "env.h"

int parse_number(char *str, uint8_t base) {
    uint8_t  digit = 0;
    uint16_t value = 0;

    while(*str) {
        if(*str >= '0' && *str <= '9')
            digit = *str - '0';
        else if(*str >= 'A' && *str <= 'F')
            digit = *str - 'A' + 10;
        else if(*str >= 'a' && *str <= 'f')
            digit = *str - 'a' + 10;
        else
            return value;

        if (digit >= base)
            return value;

        value = value * base + digit;
        str ++;
    }

    return value;
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
            switch(toupper((char)*blaster)) {
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
    printf("BLASTER: ");
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
