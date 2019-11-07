#include <hash.h>
#include "vm/suppage.h"
#include "threads/vaddr.h"

unsigned hash_func (const struct hash_elem *, void* UNUSED);
bool hash_less (const struct hash_elem *, const struct hash_elem *, void* UNUSED);

unsigned
hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  const struct SPT_entry *p = hash_entry (e, struct SPT_entry, elem);
  return hash_bytes (&p->page, sizeof (p->page));
}

bool
hash_less (const struct hash_elem *a, const struct hash_elem *b, void* aux UNUSED)
{
  const struct SPT_entry *a_ = hash_entry (a, struct SPT_entry, elem);
  const struct SPT_entry *b_ = hash_entry (b, struct SPT_entry, elem);

  return a_->page < b_->page;
}

void
SPT_init (struct hash *SPT)
{
  hash_init (SPT, hash_func, hash_less, NULL);
}

struct SPT_entry*
SPT_lookup (struct hash SPT, void *fault_addr)
{
  struct SPT_entry entry;
  struct hash_elem *e;

  entry.page = pg_round_down (fault_addr);
  e = hash_find (&SPT, &entry.elem);

  return e != NULL ? hash_entry (e, struct SPT_entry, elem) : NULL;
}
