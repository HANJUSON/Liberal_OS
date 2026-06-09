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

  // Isolate from any persisted /cache.bin records from a prior run.
  cacheclear(1);

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

  // (5) semantic (paraphrase) hit: stored under one phrasing, retrieved
  //     under a stopword-padded paraphrase that reduces to the same word
  //     set ("list files" ~= "please list the files").
  cacheset("parser", "list files", "LS-OUTPUT");
  out[0] = 0;
  fails += expect("semantic paraphrase hit",
                  cacheget("parser", "please list the files", out, sizeof(out)), 1);
  if(strcmp(out, "LS-OUTPUT") != 0){
    printf("  FAIL: semantic value mismatch out=%s\n", out);
    fails++;
  } else {
    printf("  ok: semantic value matches\n");
  }

  // (6) an unrelated prompt does not falsely hit semantically.
  fails += expect("unrelated semantic miss",
                  cacheget("parser", "reboot the database server now", out, sizeof(out)), 0);

  // (7) disk overlay: a set persists to /cache.bin; after clearing the RAM
  //     slots only (simulating eviction/reboot), a get must still hit via a
  //     sequential disk scan and promote the record back into RAM.
  cacheset("rootcause", "persist this entry", "DISK-PERSISTED");
  cacheclear(0);                       // RAM only; /cache.bin retains it
  out[0] = 0;
  fails += expect("disk-scan hit after RAM clear",
                  cacheget("rootcause", "persist this entry", out, sizeof(out)), 1);
  if(strcmp(out, "DISK-PERSISTED") != 0){
    printf("  FAIL: disk value mismatch out=%s\n", out);
    fails++;
  } else {
    printf("  ok: disk value matches\n");
  }

  if(fails == 0)
    printf("CACHE_TEST_PASS\n");
  else
    printf("CACHE_TEST_FAIL %d\n", fails);
  exit(fails == 0 ? 0 : 1);
}
