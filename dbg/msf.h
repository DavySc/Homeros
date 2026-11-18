/* Reader for the container a PDB is stored in (MSF 7.00): a file of pages, a directory of
   streams, each stream a list of pages. Follows RAD's msf_parse.c, without the 2.00 format
   that compilers stopped writing in 2002. */
#ifndef DBG_MSF_H
#define DBG_MSF_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  const uint8_t *file;
  uint64_t size;
  uint32_t page, pages;   /* page size; whole pages in the file */
  uint32_t streams;
  uint8_t *dir;           /* the directory in one piece: count, size per stream, page numbers */
  uint32_t dir_size;
} Msf;

bool msf_open(Msf *m, const void *data, uint64_t size);   /* false: not MSF 7.00, or damaged */
void msf_close(Msf *m);
/* Stream i in one piece that the caller frees, its size in *n. An absent, deleted or damaged
   stream is an empty one: null and 0. */
uint8_t *msf_stream(const Msf *m, uint32_t i, uint32_t *n);
#endif
