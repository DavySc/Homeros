/* Little-endian fields at any alignment. Every file format here is little-endian and so is
   every machine this runs on, so a field is read by copying its bytes. */
#ifndef DBG_LE_H
#define DBG_LE_H
#include <stdint.h>
#include <string.h>
static inline uint16_t le16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static inline uint32_t le32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
#endif
