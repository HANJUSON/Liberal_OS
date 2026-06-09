// Liberal_OS Pattern B (T-85/T-86): kernel response cache.
//
// T-85: FNV-1a exact-match RAM slots.
// T-86: MinHash signatures over the prompt's word set give semantic
//       (paraphrase) hits — when an exact lookup misses, a query whose
//       word set overlaps a stored entry's by at least the Jaccard
//       threshold (estimated from matching MinHash positions) hits. Words
//       are ASCII-lowercased, punctuation-split, and stopword-filtered, so
//       e.g. "list files" and "please list the files" reduce to the same
//       word set and share an identical signature.
//
// See cache.h. Integer-only (no floating point).

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "cache.h"

// From string.c / spinlock.c (declared locally to keep this unit
// self-contained without pulling in the full defs.h surface).
char* safestrcpy(char*, const char*, int);
int   strncmp(const char*, const char*, uint);
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

// Common words carry no semantic weight; dropping them lets paraphrases
// with different filler reduce to the same content word set.
static const char *stopwords[] = {
  "the", "a", "an", "please", "to", "of", "and", "is", "are", "in", "on",
  "at", "for", "with", "this", "that", "it", "be", "can", "you", "me", 0,
};

static int
is_stopword(const char *w)
{
  for (int i = 0; stopwords[i]; i++) {
    int j = 0;
    while (stopwords[i][j] && stopwords[i][j] == w[j])
      j++;
    if (stopwords[i][j] == 0 && w[j] == 0)
      return 1;
  }
  return 0;
}

// Fold one word into the MinHash signature: two independent base hashes
// give CACHE_SIG_K hash functions via double hashing (h1 + k*h2), and each
// signature position keeps the running minimum.
static void
sig_fold_word(uint64 *sig, const char *word, int len)
{
  uint64 h1 = 1469598103934665603ULL;
  uint64 h2 = 1099511628211ULL;
  for (int i = 0; i < len; i++) {
    h1 = (h1 ^ (uchar)word[i]) * 1099511628211ULL;
    h2 = (h2 ^ (uchar)word[i]) * 1469598103934665603ULL;
  }
  h2 |= 1;                                 // nonzero/odd for double hashing
  for (int k = 0; k < CACHE_SIG_K; k++) {
    uint64 hk = h1 + (uint64)k * h2;
    if (hk < sig[k])
      sig[k] = hk;
  }
}

// Compute the MinHash signature of a prompt: lowercase ASCII, split on any
// non-alphanumeric byte, drop stopwords, fold each remaining word.
static void
cache_sig(const char *prompt, uint64 *sig)
{
  char word[32];
  int wl = 0;

  for (int k = 0; k < CACHE_SIG_K; k++)
    sig[k] = ~(uint64)0;                    // identity for min()

  for (const char *p = prompt; ; p++) {
    char c = *p;
    if (c >= 'A' && c <= 'Z')
      c = c - 'A' + 'a';
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      if (wl < (int)sizeof(word) - 1)
        word[wl++] = c;
    } else {
      if (wl > 0) {
        word[wl] = 0;
        if (!is_stopword(word))
          sig_fold_word(sig, word, wl);
        wl = 0;
      }
      if (c == 0)
        break;
    }
  }
}

// Number of signature positions where two signatures agree (∝ Jaccard).
static int
sig_match(const uint64 *a, const uint64 *b)
{
  int m = 0;
  for (int k = 0; k < CACHE_SIG_K; k++)
    if (a[k] == b[k])
      m++;
  return m;
}

int
cache_get(const char *role, const char *prompt, char *out, int outmax)
{
  uint64 k = cache_fnv1a(role, prompt);
  uint64 qsig[CACHE_SIG_K];
  int best = -1, best_match = -1;

  acquire(&cache.lock);

  // Exact match first.
  for (int i = 0; i < CACHE_NSLOT; i++) {
    if (cache.slot[i].key == k) {
      if (out && outmax > 0)
        safestrcpy(out, cache.slot[i].val, outmax);
      release(&cache.lock);
      return 1;
    }
  }

  // Semantic match: best same-role slot whose signature overlap clears the
  // Jaccard threshold.
  cache_sig(prompt, qsig);
  for (int i = 0; i < CACHE_NSLOT; i++) {
    if (cache.slot[i].key == 0)
      continue;
    if (strncmp(cache.slot[i].role, role, CACHE_ROLELEN) != 0)
      continue;
    int m = sig_match(qsig, cache.slot[i].sig);
    if (m > best_match) {
      best_match = m;
      best = i;
    }
  }
  if (best >= 0 && best_match * CACHE_SIG_DEN >= CACHE_SIG_NUM * CACHE_SIG_K) {
    if (out && outmax > 0)
      safestrcpy(out, cache.slot[best].val, outmax);
    release(&cache.lock);
    return 1;
  }

  release(&cache.lock);
  return 0;
}

int
cache_set(const char *role, const char *prompt, const char *val)
{
  uint64 k = cache_fnv1a(role, prompt);
  uint64 sig[CACHE_SIG_K];
  int stored = 0;

  cache_sig(prompt, sig);

  acquire(&cache.lock);
  // Update an existing entry for this key first, else take an empty slot.
  int target = -1;
  for (int i = 0; i < CACHE_NSLOT; i++) {
    if (cache.slot[i].key == k) {
      target = i;
      break;
    }
  }
  if (target < 0) {
    for (int i = 0; i < CACHE_NSLOT; i++) {
      if (cache.slot[i].key == 0) {
        target = i;
        break;
      }
    }
  }
  if (target >= 0) {
    cache.slot[target].key = k;
    safestrcpy(cache.slot[target].role, role, CACHE_ROLELEN);
    for (int j = 0; j < CACHE_SIG_K; j++)
      cache.slot[target].sig[j] = sig[j];
    safestrcpy(cache.slot[target].val, val, CACHE_VALLEN);
    stored = 1;
  }
  release(&cache.lock);
  return stored;
}
