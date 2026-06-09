// Liberal_OS Pattern A (T-81): exercise the sys_verifyfix(27) syscall.
//
// Builds fix proposals in user space and asserts the kernel verifier's
// verdicts: a well-formed allowed proposal passes, while range, protected
// -target, whitelist, and missing-field violations each fail with their
// specific code. Prints VERIFIER_TEST_PASS on full success so autotest /
// test_verifier.sh can grep for it.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/verifier.h"
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

static void
mkproposal(struct fix_proposal *p, const char *role, const char *target,
           int action, int severity, int retry_hint)
{
  memset(p, 0, sizeof(*p));
  if(role)
    strcpy(p->role, role);
  if(target)
    strcpy(p->target, target);
  p->action = action;
  p->severity = severity;
  p->retry_hint = retry_hint;
}

int
main(void)
{
  int fails = 0;
  char reason[64];
  struct fix_proposal p;

  // (1) well-formed, allowed action → PASS.
  mkproposal(&p, "fixsuggest", "parser", FIX_ACTION_REQUEUE, 2, 1);
  fails += expect("good proposal", verifyfix(&p, reason, sizeof(reason)), VERIFY_OK);

  // (2) severity out of [0,3] → RANGE violation.
  mkproposal(&p, "fixsuggest", "parser", FIX_ACTION_REPORT, 9, 0);
  fails += expect("bad: severity range", verifyfix(&p, reason, sizeof(reason)), VERIFY_ERR_RANGE);

  // (3) destructive action against a protected target → PROTECTED violation.
  mkproposal(&p, "fixsuggest", "init", FIX_ACTION_KILL, 3, 0);
  fails += expect("bad: protected target", verifyfix(&p, reason, sizeof(reason)), VERIFY_ERR_PROTECTED);

  // (4) destructive action, non-protected target → ACTION (whitelist) violation.
  mkproposal(&p, "fixsuggest", "parser", FIX_ACTION_RESET, 1, 0);
  fails += expect("bad: action whitelist", verifyfix(&p, reason, sizeof(reason)), VERIFY_ERR_ACTION);

  // (5) missing required field (empty role) → FIELD violation.
  mkproposal(&p, 0, "parser", FIX_ACTION_REPORT, 0, 0);
  fails += expect("bad: empty role", verifyfix(&p, reason, sizeof(reason)), VERIFY_ERR_FIELD);

  // (6) checkpoint/restore round-trip: save a good state, clobber a local
  //     copy, restore, and confirm the original bytes come back. This is
  //     the rollback primitive Pattern A uses after a failed verify.
  {
    char saved[16], scratch[16];
    memset(saved, 0, sizeof(saved));
    strcpy(saved, "ACCEPTED-v1");
    fails += expect("checkpoint len", checkpoint(saved, sizeof(saved)), (int)sizeof(saved));
    memset(scratch, 0, sizeof(scratch));
    strcpy(scratch, "CLOBBERED");
    fails += expect("restore len", restore(scratch, sizeof(scratch)), (int)sizeof(saved));
    fails += expect("restore data matches", strcmp(scratch, "ACCEPTED-v1") == 0, 1);
  }

  if(fails == 0)
    printf("VERIFIER_TEST_PASS\n");
  else
    printf("VERIFIER_TEST_FAIL %d\n", fails);
  exit(fails == 0 ? 0 : 1);
}
