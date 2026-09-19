

#include "dlink_internal.h"

#include <stdlib.h>

void *dlink_alloc_pages(size_t size) {
  size = (size + 0xFFF) & ~(size_t)0xFFF;
  if (size == 0) {
    size = 0x1000;
  }
  return aligned_alloc(0x1000, size);
}

void dlink_free_pages(void *pages) {
  free(pages);
}
