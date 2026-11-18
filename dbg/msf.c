#include "msf.h"
#include <stdlib.h>
#include <string.h>

static const char magic[32] = "Microsoft C/C++ MSF 7.00\r\n\032DS\0\0";
enum { HEADER_ROOTS = 52 };   /* after the magic: page size, free map, page count, directory size, 0 */

static uint32_t u32_at(const uint8_t *p, uint64_t i)
{
  uint32_t v;
  memcpy(&v, p + i * 4, 4);
  return v;
}
static uint64_t pages_for(const Msf *m, uint64_t bytes) { return (bytes + m->page - 1) / m->page; }

/* n bytes from the pages numbered list[0..], of which `have` are readable; null if n is 0, the
   list is too short or a page is outside the file. The map of the directory, the directory and
   every stream are each read this way. */
static uint8_t *gather(const Msf *m, const uint8_t *list, uint64_t have, uint32_t n)
{
  uint64_t count = pages_for(m, n);
  if (n == 0 || n > m->size || count > have) return 0;
  uint8_t *out = malloc(n);
  for (uint64_t i = 0; out && i < count; i += 1) {
    uint32_t pn = u32_at(list, i), take = n - i * m->page < m->page ? (uint32_t)(n - i * m->page) : m->page;
    if (pn >= m->pages) {
      free(out);
      return 0;
    }
    memcpy(out + i * m->page, m->file + (uint64_t)pn * m->page, take);
  }
  return out;
}

bool msf_open(Msf *m, const void *data, uint64_t size)
{
  *m = (Msf){.file = data, .size = size};
  if (size < HEADER_ROOTS || memcmp(data, magic, sizeof magic) != 0) return false;
  m->page = u32_at(m->file, 8);
  if (m->page < 512 || m->page > 32768) return false;
  m->pages = u32_at(m->file, 10) < size / m->page ? u32_at(m->file, 10) : (uint32_t)(size / m->page);
  m->dir_size = u32_at(m->file, 11);
  /* the header lists the pages of the map, the map lists the pages of the directory */
  uint64_t dir_pages = pages_for(m, m->dir_size), map_pages = pages_for(m, dir_pages * 4);
  if (m->pages == 0 || HEADER_ROOTS + map_pages * 4 > m->page) return false;
  uint8_t *map = gather(m, m->file + HEADER_ROOTS, map_pages, (uint32_t)(dir_pages * 4));
  m->dir = map ? gather(m, map, dir_pages, m->dir_size) : 0;
  free(map);
  if (!m->dir || m->dir_size < 4 || u32_at(m->dir, 0) > (m->dir_size - 4) / 4) {
    msf_close(m);
    return false;
  }
  m->streams = u32_at(m->dir, 0);
  return true;
}

void msf_close(Msf *m)
{
  free(m->dir);
  m->dir = 0;
}

/* a deleted stream has size 0xffffffff */
static uint32_t stream_size(const Msf *m, uint32_t i) { uint32_t n = u32_at(m->dir, 1 + i); return n == UINT32_MAX ? 0 : n; }

uint8_t *msf_stream(const Msf *m, uint32_t i, uint32_t *n)
{
  *n = 0;
  if (i >= m->streams) return 0;
  uint64_t first = 1 + (uint64_t)m->streams, words = m->dir_size / 4;
  for (uint32_t j = 0; j < i; j += 1) first += pages_for(m, stream_size(m, j));
  uint8_t *out = first < words ? gather(m, m->dir + first * 4, words - first, stream_size(m, i)) : 0;
  if (out) *n = stream_size(m, i);
  return out;
}
