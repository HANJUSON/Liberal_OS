// Liberal_OS Pattern A: pure-function verifier for LLM fix proposals.
// No proc-table access and no floating point — integer logic only.
// See verifier.h and GOAL_PATTERNS.md §3.

#include "types.h"
#include "verifier.h"

// Pulled from string.c. Declared locally (not via defs.h) so this unit
// stays standalone and trivially unit-testable.
int   strncmp(const char*, const char*, uint);
char* safestrcpy(char*, const char*, int);

// Protected roles: destructive proposals against these are rejected.
static const char *verify_protected_roles[VERIFY_NPROT] = {
  "init", "sh", "evaluator",
};

// True if `action` would mutate/destroy a target (i.e., not one of the
// report/annotate/requeue whitelist actions).
static int
is_destructive(int action)
{
  return action != FIX_ACTION_REPORT &&
         action != FIX_ACTION_ANNOTATE &&
         action != FIX_ACTION_REQUEUE;
}

// True if `action` is on the allowed whitelist.
static int
is_allowed(int action)
{
  return action == FIX_ACTION_REPORT ||
         action == FIX_ACTION_ANNOTATE ||
         action == FIX_ACTION_REQUEUE;
}

// A fixed-length text field is valid when it is non-empty and
// NUL-terminated strictly within [1, max).
static int
field_ok(const char *f, int max)
{
  int n = 0;
  while (n < max && f[n] != '\0')
    n++;
  return n >= 1 && n < max;
}

// Whether the proposal target is protected: it matches a protected role
// name, or the kernel resolved it to a protected pid via the snapshot.
static int
target_is_protected(const struct fix_proposal *p, const struct sys_snapshot *s)
{
  for (int i = 0; i < VERIFY_NPROT; i++) {
    // strncmp stops at the shorter string's NUL, so an exact match of
    // the bounded target buffer against the role name returns 0.
    if (strncmp(p->target, verify_protected_roles[i], FIX_TARGET_LEN) == 0)
      return 1;
  }
  return s != 0 && s->target_protected;
}

static void
set_reason(char *reason, int rlen, const char *msg)
{
  if (reason != 0 && rlen > 0)
    safestrcpy(reason, msg, rlen);
}

int
verify_fix(const struct fix_proposal *p, const struct sys_snapshot *s,
           char *reason, int rlen)
{
  if (p == 0) {
    set_reason(reason, rlen, "null proposal");
    return VERIFY_ERR_FIELD;
  }

  // (1) Structural integrity: required text fields present and bounded.
  if (!field_ok(p->role, FIX_ROLE_LEN)) {
    set_reason(reason, rlen, "role field missing or oversized");
    return VERIFY_ERR_FIELD;
  }
  if (!field_ok(p->target, FIX_TARGET_LEN)) {
    set_reason(reason, rlen, "target field missing or oversized");
    return VERIFY_ERR_FIELD;
  }

  // (2) Numeric ranges.
  if (p->severity < FIX_SEVERITY_MIN || p->severity > FIX_SEVERITY_MAX) {
    set_reason(reason, rlen, "severity out of range [0,3]");
    return VERIFY_ERR_RANGE;
  }
  if (p->retry_hint < 0) {
    set_reason(reason, rlen, "retry_hint negative");
    return VERIFY_ERR_RANGE;
  }

  // (4) Protected-process rule, checked before the generic whitelist so
  //     a destructive-against-protected proposal gets the specific code.
  if (is_destructive(p->action) && target_is_protected(p, s)) {
    set_reason(reason, rlen, "destructive action on protected target");
    return VERIFY_ERR_PROTECTED;
  }

  // (3) Action whitelist.
  if (!is_allowed(p->action)) {
    set_reason(reason, rlen, "action not on whitelist");
    return VERIFY_ERR_ACTION;
  }

  set_reason(reason, rlen, "ok");
  return VERIFY_OK;
}
