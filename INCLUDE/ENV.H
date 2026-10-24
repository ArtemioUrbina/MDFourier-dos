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

/* /W: the WSS codec of SB Pro compatibles like the OPL3-SAx */
#define WSS_UNSET   0       /* found one: say so and stop */
#define WSS_OFF     1       /* /W:OFF, no 16-bit */
#define WSS_AUTO    2       /* /W:AUTO, read and probe its settings */
#define WSS_MANUAL  3       /* /W:port,irq,dma */

typedef struct {
    int      mode;
    unsigned port;
    int      irq, dma;
} wss_cfg_t;

extern wss_cfg_t wss_cfg;

/* 1 if the /W: text is valid */
int env_parse_wss(char *arg);

/* /V: details that only matter when something is wrong */
extern int verbose;

int env_get_blaster(blaster_cfg_t *cfg);
void print_env(blaster_cfg_t *cfg);

#endif
