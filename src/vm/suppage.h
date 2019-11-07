#include <hash.h>

struct SPT_entry
  {
    uint32_t *page;
    struct hash_elem elem;
    bool evicted;
  };

void SPT_init (struct hash *);
struct SPT_entry* SPT_lookup (struct hash, void *);
