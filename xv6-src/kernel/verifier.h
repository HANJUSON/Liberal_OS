#ifndef VERIFIER_H
#define VERIFIER_H

// Liberal_OS Pattern A (verify+rollback): kernel-side verifier for
// LLM-proposed fixes. The LLM is only a *proposer*; the kernel holds
// final authority. verify_fix() is a pure function (integer-only, no
// floating point, no proc-table access). The syscall layer
// (sys_verifyfix in sysproc.c, wired in T-81) resolves any process
// state into a sys_snapshot and passes it in, so this stays testable
// in isolation. See GOAL_PATTERNS.md §3.

// Fixed field length bounds (structural integrity).
#define FIX_ROLE_LEN    16
#define FIX_TARGET_LEN  16

// Allowed (non-destructive) actions — the whitelist.
#define FIX_ACTION_REPORT    0
#define FIX_ACTION_ANNOTATE  1
#define FIX_ACTION_REQUEUE   2
// Destructive actions — never allowed against protected targets and
// not on the whitelist at all.
#define FIX_ACTION_KILL      3
#define FIX_ACTION_RESET     4

// Numeric field ranges.
#define FIX_SEVERITY_MIN  0
#define FIX_SEVERITY_MAX  3

// Violation codes (verify_fix return; 0 == PASS, negative == violation).
#define VERIFY_OK             0
#define VERIFY_ERR_FIELD     (-1)   // missing / oversized required field
#define VERIFY_ERR_RANGE     (-2)   // numeric field out of range
#define VERIFY_ERR_PROTECTED (-3)   // destructive action vs protected target
#define VERIFY_ERR_ACTION    (-4)   // action not on the whitelist

// Number of protected roles (names live in verifier.c).
#define VERIFY_NPROT 3

// A fix proposal as parsed from fix-suggester / evaluator output.
struct fix_proposal {
  char role[FIX_ROLE_LEN];     // proposing agent role
  char target[FIX_TARGET_LEN]; // role/name the fix would act on
  int  action;                 // one of FIX_ACTION_*
  int  severity;               // expected 0..3
  int  retry_hint;             // expected >= 0
};

// Kernel-resolved process state the verifier needs. Filled by the
// syscall layer so the verifier never walks the proc table itself.
struct sys_snapshot {
  int target_pid;        // resolved pid of the proposal target, or -1
  int target_protected;  // 1 if the resolved target is a protected proc
  int nproc;             // live proc count (context only)
};

// Verify one fix proposal against a snapshot. Returns VERIFY_OK (0) on
// pass or a negative VERIFY_ERR_* on the first violation found. On
// violation a short human-readable cause is written into reason[0,rlen).
int verify_fix(const struct fix_proposal *p, const struct sys_snapshot *s,
               char *reason, int rlen);

#endif // VERIFIER_H
