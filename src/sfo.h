/*
 * sfo.h — minimal PARAM.SFO parser (pure C, host-testable)
 */

#ifndef SFO_H
#define SFO_H

typedef struct {
    const char *key;      /* pointer into the SFO buffer */
    unsigned int fmt;     /* 0x0204 = string, 0x0404 = int */
    unsigned int len;
    unsigned int max_len;
    unsigned int data_off; /* absolute offset of data in buffer */
} sfo_entry_t;

/* Parse a PARAM.SFO buffer. Returns entry count (or negative on error). */
int sfo_parse(const unsigned char *buf, unsigned int size,
              sfo_entry_t *out, int max_entries);

/* Get string value of an entry (empty string if not string fmt). */
const char *sfo_get_str(const unsigned char *buf, const sfo_entry_t *e);

/* Get integer value of an entry (0 if not int fmt). */
unsigned int sfo_get_int(const unsigned char *buf, const sfo_entry_t *e);

#endif /* SFO_H */