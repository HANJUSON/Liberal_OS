#ifndef CACHE_H
#define CACHE_H

// Liberal_OS Pattern B (T-85): kernel-side response cache.
//
// An agent consults this cache before issuing a PROXY_REQ to the host; a
// hit short-circuits the LLM call entirely (wired in T-88). This file is
// the exact-match layer: keys are FNV-1a 64-bit hashes of "role|prompt"
// (the prompt text itself is not stored), held in a fixed RAM slot table.
// A zero key marks an empty slot, so a real hash that lands on 0 is
// promoted to 1. T-86 adds a MinHash signature (sig[]) for semantic
// (paraphrase) hits; T-87 adds the /cache.bin disk overlay.

#define CACHE_NSLOT   64        // RAM slot table size
#define CACHE_VALLEN  192       // stored value length (NUL-terminated)
#define CACHE_ROLELEN 16        // agent role string length
#define CACHE_SIG_K   16        // MinHash signature length (# hash funcs)
// Semantic-hit threshold as an integer ratio: a query is a semantic hit
// when (matching signature positions) / CACHE_SIG_K >= SIG_NUM / SIG_DEN.
#define CACHE_SIG_NUM 2
#define CACHE_SIG_DEN 5         // 2/5 = 0.40 estimated Jaccard

struct cache_slot {
  uint64 key;                   // FNV-1a(role|prompt); 0 == empty
  char   role[CACHE_ROLELEN];   // role, for role-scoped semantic match
  uint64 sig[CACHE_SIG_K];      // MinHash signature of the prompt's words
  char   val[CACHE_VALLEN];
};

void   cacheinit(void);

// FNV-1a 64-bit hash of "role|prompt". Never returns 0 (promoted to 1).
uint64 cache_fnv1a(const char *role, const char *prompt);

// Exact-match lookup. On hit copies the value into out (<= outmax, NUL-
// terminated) and returns 1; on miss returns 0 and leaves out untouched.
int    cache_get(const char *role, const char *prompt, char *out, int outmax);

// Insert or update the value for (role, prompt). Returns 1 on store,
// 0 if the table is full.
int    cache_set(const char *role, const char *prompt, const char *val);

#endif // CACHE_H
