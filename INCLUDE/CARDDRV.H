#ifndef CARD_DRV_H
#define CARD_DRV_H
#include <stdint.h>

typedef struct {
    char *name;
    int (*detect)();
    void (*init)();
    void (*write)(uint8_t reg, uint8_t val);
    void (*reset)();
} fm_driver_t;

#endif
