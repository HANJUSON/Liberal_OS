// Liberal_OS Pattern B (T-85): exercise the kernel exact-match response
// cache via the cacheget(30)/cacheset(31) syscalls.
//
// Asserts: a miss before any set; an exact hit returning the stored value
// after set; a miss for a different prompt; and role-sensitivity (same
// prompt under a different role misses). Prints CACHE_TEST_PASS on success
// so autotest / test_cache.sh can grep for it. T-86 adds semantic
// (paraphrase) hit assertions to this file.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static int
expect(const char *label, int got, int want)
{
  if(got == want){
    printf("  ok: %s (rc=%d)\n", label, got);
    return 0;
  }
  printf("  FAIL: %s rc=%d want=%d\n", label, got, want);
  return 1;
}

int
main(void)
{
  int fails = 0;
  char out[192];

  // (1) miss before anything is stored.
  fails += expect("miss before set", cacheget("parser", "disk full at /var/log", out, sizeof(out)), 0);

  // (2) store, then exact hit returns the stored value.
  cacheset("parser", "disk full at /var/log", "FIX-restart-logrotate");
  out[0] = 0;
  int hit = cacheget("parser", "disk full at /var/log", out, sizeof(out));
  fails += expect("exact hit", hit, 1);
  if(strcmp(out, "FIX-restart-logrotate") != 0){
    printf("  FAIL: hit value mismatch out=%s\n", out);
    fails++;
  } else {
    printf("  ok: hit value matches\n");
  }

  // (3) a different prompt under the same role misses.
  fails += expect("distinct prompt miss", cacheget("parser", "connection refused", out, sizeof(out)), 0);

  // (4) the same prompt under a different role misses (key includes role).
  fails += expect("role-sensitive miss", cacheget("classifier", "disk full at /var/log", out, sizeof(out)), 0);

  if(fails == 0)
    printf("CACHE_TEST_PASS\n");
  else
    printf("CACHE_TEST_FAIL %d\n", fails);
  exit(fails == 0 ? 0 : 1);
}
