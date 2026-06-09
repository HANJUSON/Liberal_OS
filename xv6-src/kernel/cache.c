// Liberal_OS Pattern B (T-85/T-86/T-87): kernel response cache.
//
// T-85: FNV-1a exact-match RAM slots.
// T-86: MinHash signatures over the prompt's word set give semantic
//       (paraphrase) hits when an exact lookup misses.
// T-87: /cache.bin disk overlay — every cache_set also appends a fixed-
//       size record to /cache.bin, and a RAM miss falls through to a
//       sequential scan of that file; a disk hit is promoted back into a
//       RAM slot. This makes the cache survive RAM-slot eviction / reboot.
//
// Integer-only (no floating point). Disk I/O runs in process context
// (from the cacheget/cacheset/cacheclear syscalls), never while holding
// the RAM spinlock.

#include "types.h"
#include "riscv.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "defs.h"
#include "cache.h"

#define CACHE_DISK_PATH "/cache.bin"

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

static int
sig_over_threshold(int match)
{
  return match * CACHE_SIG_DEN >= CACHE_SIG_NUM * CACHE_SIG_K;
}

// ---- disk overlay (/cache.bin) ------------------------------------------
// All three run in process context (syscall path) and must NOT be called
// while holding cache.lock.

static void
cache_disk_append(const struct cache_slot *rec)
{
  struct inode *ip;

  begin_op();
  if ((ip = namei(CACHE_DISK_PATH)) == 0) {
    end_op();
    return;                                // overlay file absent: RAM-only
  }
  ilock(ip);
  writei(ip, 0, (uint64)rec, ip->size, sizeof(*rec));
  iunlockput(ip);
  end_op();
}

// Scan /cache.bin for (key / role+signature). On hit copies the value into
// out and, if promote != 0, the full record for RAM promotion. Last write
// wins for exact-key matches.
static int
cache_disk_scan(const char *role, uint64 key, const uint64 *qsig,
                char *out, int outmax, struct cache_slot *promote)
{
  struct inode *ip;
  struct cache_slot rec, exact, sem;
  int have_exact = 0, have_sem = 0, sem_match = -1;
  uint off, sz;

  begin_op();
  if ((ip = namei(CACHE_DISK_PATH)) == 0) {
    end_op();
    return 0;
  }
  ilock(ip);
  sz = ip->size;
  for (off = 0; off + sizeof(rec) <= sz; off += sizeof(rec)) {
    if (readi(ip, 0, (uint64)&rec, off, sizeof(rec)) != sizeof(rec))
      break;
    if (rec.key == key) {
      exact = rec;
      have_exact = 1;                      // keep scanning: last write wins
    } else if (strncmp(rec.role, role, CACHE_ROLELEN) == 0) {
      int m = sig_match(qsig, rec.sig);
      if (m > sem_match) {
        sem_match = m;
        sem = rec;
        have_sem = 1;
      }
    }
  }
  iunlockput(ip);
  end_op();

  if (have_exact) {
    if (out && outmax > 0) safestrcpy(out, exact.val, outmax);
    if (promote) *promote = exact;
    return 1;
  }
  if (have_sem && sig_over_threshold(sem_match)) {
    if (out && outmax > 0) safestrcpy(out, sem.val, outmax);
    if (promote) *promote = sem;
    return 1;
  }
  return 0;
}

static void
cache_disk_truncate(void)
{
  struct inode *ip;

  begin_op();
  if ((ip = namei(CACHE_DISK_PATH)) == 0) {
    end_op();
    return;
  }
  ilock(ip);
  itrunc(ip);
  iunlockput(ip);
  end_op();
}

// ---- RAM helpers --------------------------------------------------------

// Insert/overwrite a full record into a RAM slot. Caller holds cache.lock.
static void
ram_put(const struct cache_slot *rec)
{
  int target = -1;
  for (int i = 0; i < CACHE_NSLOT; i++) {
    if (cache.slot[i].key == rec->key) { target = i; break; }
  }
  if (target < 0) {
    for (int i = 0; i < CACHE_NSLOT; i++) {
      if (cache.slot[i].key == 0) { target = i; break; }
    }
  }
  if (target >= 0)
    cache.slot[target] = *rec;
}

// ---- public API ---------------------------------------------------------

int
cache_get(const char *role, const char *prompt, char *out, int outmax)
{
  uint64 k = cache_fnv1a(role, prompt);
  uint64 qsig[CACHE_SIG_K];
  struct cache_slot promo;
  int best = -1, best_match = -1;

  cache_sig(prompt, qsig);                  // pure; safe outside the lock

  acquire(&cache.lock);
  // Exact RAM match.
  for (int i = 0; i < CACHE_NSLOT; i++) {
    if (cache.slot[i].key == k) {
      if (out && outmax > 0)
        safestrcpy(out, cache.slot[i].val, outmax);
      release(&cache.lock);
      return 1;
    }
  }
  // Semantic RAM match.
  for (int i = 0; i < CACHE_NSLOT; i++) {
    if (cache.slot[i].key == 0)
      continue;
    if (strncmp(cache.slot[i].role, role, CACHE_ROLELEN) != 0)
      continue;
    int m = sig_match(qsig, cache.slot[i].sig);
    if (m > best_match) { best_match = m; best = i; }
  }
  if (best >= 0 && sig_over_threshold(best_match)) {
    if (out && outmax > 0)
      safestrcpy(out, cache.slot[best].val, outmax);
    release(&cache.lock);
    return 1;
  }
  release(&cache.lock);

  // RAM miss: fall through to the disk overlay, promoting any hit.
  if (cache_disk_scan(role, k, qsig, out, outmax, &promo)) {
    acquire(&cache.lock);
    ram_put(&promo);
    release(&cache.lock);
    return 1;
  }
  return 0;
}

int
cache_set(const char *role, const char *prompt, const char *val)
{
  struct cache_slot rec;
  int stored;

  rec.key = cache_fnv1a(role, prompt);
  safestrcpy(rec.role, role, CACHE_ROLELEN);
  cache_sig(prompt, rec.sig);
  safestrcpy(rec.val, val, CACHE_VALLEN);

  acquire(&cache.lock);
  // ram_put always succeeds while a slot is free; report table-full as 0.
  int free_or_match = 0;
  for (int i = 0; i < CACHE_NSLOT; i++) {
    if (cache.slot[i].key == rec.key || cache.slot[i].key == 0) {
      free_or_match = 1;
      break;
    }
  }
  if (free_or_match)
    ram_put(&rec);
  stored = free_or_match;
  release(&cache.lock);

  if (stored)
    cache_disk_append(&rec);               // persist to the overlay
  return stored;
}

void
cache_clear(int disk)
{
  acquire(&cache.lock);
  for (int i = 0; i < CACHE_NSLOT; i++)
    cache.slot[i].key = 0;                  // mark every slot empty
  release(&cache.lock);

  if (disk)
    cache_disk_truncate();
}
