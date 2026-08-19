#ifndef ENV_H
#define ENV_H
#include <stdint.h>

/*
 * Struct for BLASTER=A220 I7 D1 T6 environment variable
 */

typedef struct {
    int16_t port;
    int8_t  irq;
    int8_t  dma;
    int8_t  high_dma;
    int16_t mpu_port;
    int8_t  type;
} blaster_cfg_t;

int env_get_blaster(blaster_cfg_t *cfg);
void print_env(blaster_cfg_t *cfg);

#endif
