#include <hash.h>
#include "threads/palloc.h"

struct frame_table_entry {
  struct hash_elem elem;

  struct thread *owner;
  uint32_t *frame;
  uint32_t *page;
};

void frame_table_init (void);

void *frame_alloc(enum palloc_flags flags);
