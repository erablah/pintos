#include <stdio.h>
#include <hash.h>

#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"

static struct hash frame_table;

unsigned hash_swan_func (const struct hash_elem *elem, void *aux UNUSED);
bool less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);


unsigned
hash_swan_func (const struct hash_elem *elem, void *aux UNUSED)
{
  struct frame_table_entry *fte = hash_entry(elem, struct frame_table_entry, elem);
  return hash_bytes(fte->frame, sizeof(fte->frame));
}

bool
less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct frame_table_entry *fte_a = hash_entry(a, struct frame_table_entry, elem);
  struct frame_table_entry *fte_b = hash_entry(b, struct frame_table_entry, elem);
  return fte_a->frame < fte_b->frame;
}


void
frame_table_init (void)
{
  hash_init(&frame_table, hash_swan_func, less_func, NULL);
}

void *
frame_alloc(enum palloc_flags flags)
{
  struct frame_table_entry *new = malloc(sizeof(struct frame_table_entry));

  if (new == NULL)
  {
    PANIC ("cannot allocate more entries");
  }

  new->owner = thread_current();
  new->frame = palloc_get_page(flags);

  if (new->frame == NULL)
  {
    PANIC ("cannot allocate more frames");
  }

  hash_insert(&frame_table, &new->elem);
  return new->frame;
}
