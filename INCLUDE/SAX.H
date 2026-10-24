#ifndef SAX_H
#define SAX_H

/* Yamaha OPL3-SAx (YMF711 OPL3-SA2, YMF715 OPL3-SA3) and similar */

void sax_detect();
int  sax_mixer_set();
int  sax_mixer_report();
int  sax_needs_config();

char *sax_no_16bit_reason();

#endif
