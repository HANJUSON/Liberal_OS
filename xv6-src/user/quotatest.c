// Liberal_OS T-A1: per-process memory quota test.
//
// Caps the process a few pages above its current resident size, then
// asserts that sbrk within the cap succeeds, sbrk past it is denied
// (-1), and that lifting the cap (setquota(0)) re-enables growth.
// Prints QUOTA_TEST_PASS / QUOTA_TEST_FAIL for the autotest gate.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096

static int
fail(const char *msg)
{
  printf("  FAIL: %s\n", msg);
  return 0;
}

int
main(void)
{
  int ok = 1;

  // Resident pages right now (sbrk(0) is the current program break).
  uint64 brk = (uint64)sbrk(0);
  int cur_pages = (int)((brk + PGSIZE - 1) / PGSIZE);
  int cap = cur_pages + 4;            // allow 4 more pages of heap

  setquota(cap);

  // (1) grow by 2 pages: within the cap -> must succeed.
  if (sbrk(2 * PGSIZE) == (char *)-1)
    ok = fail("grow within quota was denied");

  // (2) grow by 8 more pages: would exceed the cap -> must be denied.
  if (sbrk(8 * PGSIZE) != (char *)-1)
    ok = fail("grow past quota was allowed");

  // (3) lift the quota -> the same growth must now succeed.
  setquota(0);
  if (sbrk(8 * PGSIZE) == (char *)-1)
    ok = fail("grow after quota lifted was denied");

  if (ok)
    printf("QUOTA_TEST_PASS\n");
  else
    printf("QUOTA_TEST_FAIL\n");
  exit(0);
}
