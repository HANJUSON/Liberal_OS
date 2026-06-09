// Liberal_OS Pattern B (T-85): FNV-1a exact-match RAM response cache.
// See cache.h. Integer-only; values are short NUL-terminated strings.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "cache.h"

// From string.c / spinlock.c (declared locally to keep this unit
// self-contained without pulling in the full defs.h surface).
char* safestrcpy(char*, const char*, int);
void  initlock(struct spinlock*, char*);
void  acquire(struct spinlock*);
void  release(struct spinlock*);

static struct {
  struct spinlock lock;
  struct cache_slot slot[CACHE_NSLOT];
} cache;

void
cacheinit(void)
{
  // Slots live in BSS (zero-initialized: every key starts 0 == empty).
  initlock(&cache.lock, "cache");
}

uint64
cache_fnv1a(const char *role, const char *prompt)
{
  uint64 h = 1469598103934665603ULL;      // FNV-1a 64-bit offset basis
  const uint64 prime = 1099511628211ULL;
  const char *s;

  for (s = role; *s; s++) {
    h ^= (uchar)*s;
    h *= prime;
  }
  h ^= (uchar)'|';                         // role/prompt separator
  h *= prime;
  for (s = prompt; *s; s++) {
    h ^= (uchar)*s;
    h *= prime;
  }
  if (h == 0)
    h = 1;                                 // 0 is reserved for empty slots
  return h;
}

int
cache_get(const char *role, const char *prompt, char *out, int outmax)
{
  uint64 k = cache_fnv1a(role, prompt);
  int found = 0;

  acquire(&cache.lock);
  for (int i = 0; i < CACHE_NSLOT; i++) {
    if (cache.slot[i].key == k) {
      if (out && outmax > 0)
        safestrcpy(out, cache.slot[i].val, outmax);
      found = 1;
      break;
    }
  }
  release(&cache.lock);
  return found;
}

int
cache_set(const char *role, const char *prompt, const char *val)
{
  uint64 k = cache_fnv1a(role, prompt);
  int stored = 0;

  acquire(&cache.lock);
  // Update an existing entry for this key first.
  for (int i = 0; i < CACHE_NSLOT; i++) {
    if (cache.slot[i].key == k) {
      safestrcpy(cache.slot[i].val, val, CACHE_VALLEN);
      stored = 1;
      break;
    }
  }
  // Otherwise take the first empty slot.
  if (!stored) {
    for (int i = 0; i < CACHE_NSLOT; i++) {
      if (cache.slot[i].key == 0) {
        cache.slot[i].key = k;
        safestrcpy(cache.slot[i].val, val, CACHE_VALLEN);
        stored = 1;
        break;
      }
    }
  }
  release(&cache.lock);
  return stored;
}
