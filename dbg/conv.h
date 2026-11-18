/* PDB to RDI: the debugger reads only RDI (rdi.h), so each PDB is converted once.
   What is written, and why it differs from RAD's own conversion where it does, is in conv.c. */
#ifndef DBG_CONV_H
#define DBG_CONV_H
#include <stdint.h>
/* The RDI file for a PDB, in a block the caller frees, its size in *n; null when the PDB holds
   no debug info. */
uint8_t *rdi_from_pdb(const void *pdb, uint64_t size, uint64_t *n);
#endif
