// Liberal_OS T-34 + T-40/T-41 + T-83: evaluator agent with a kernel-
// guarded verify+rollback retry loop.
//
// Closed loop (Pattern A): for each downstream line the evaluator forms a
// candidate fix_proposal and submits it to the kernel verifier
// (verifyfix). The LLM is only a *proposer* — the kernel holds final
// authority:
//   - verify PASS  -> checkpoint() the accepted state, emit EVAL_ACCEPT.
//   - verify FAIL  -> emit EVAL_VERIFY_FAIL <reason>, restore() the last
//                     accepted state (EVAL_ROLLBACK), correct the proposal
//                     per the verdict, and retry (EVAL_RETRY) — bounded to
//                     MAX_RETRIES (T-41).
//
// Convergence is driven by the kernel VERDICT, not by a retry counter:
// after a rejected proposal the agent restores the last accepted state and
// calls amend_from_verdict(), which corrects the offending field according
// to the returned VERIFY_ERR_* code (RANGE -> clamp severity, ACTION ->
// whitelist the action, PROTECTED -> non-destructive + safe target, FIELD
// -> fall back to baseline fields). A flagged ("ERROR") line yields an
// out-of-range proposal on the first attempt (rejected with VERIFY_ERR_RANGE)
// which the verdict-driven clamp makes valid on retry — deterministically
// reproducing VERIFY FAIL -> ROLLBACK -> RETRY -> ACCEPT.
//
// EVAL_RETRY is still emitted on every retry so the host daemon's
// eval_retries counter (and tests/test_verifier.sh) can observe the loop.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/verifier.h"
#include "user/user.h"
#include "user/proxy_client.h"

#define MAX_RETRIES 3

static void
emit(int fd, const char *tag, const char *body)
{
  char buf[640];
  int p = 0, i;
  for (i = 0; tag[i] && p < (int)sizeof(buf) - 1; i++) buf[p++] = tag[i];
  for (i = 0; body[i] && p < (int)sizeof(buf) - 1; i++) buf[p++] = body[i];
  buf[p++] = '\n';
  write(fd, buf, p);
}

static int
quality_bad(const char *s)
{
  // Quality rule: presence of "ERROR" anywhere in the response means
  // upstream didn't successfully classify/diagnose, so the suggested fix
  // is suspect.
  for (int i = 0; s[i]; i++) {
    if (s[i] == 'E' && s[i+1] == 'R' && s[i+2] == 'R'
        && s[i+3] == 'O' && s[i+4] == 'R')
      return 1;
  }
  return 0;
}

static void
strcopy(char *dst, const char *src, int max)
{
  int i;
  for (i = 0; i < max - 1 && src[i]; i++) dst[i] = src[i];
  dst[i] = 0;
}

// A known-good baseline proposal: in-range, non-destructive, whitelisted.
// Seeded into the checkpoint slot so the first rollback restores real state.
static void
build_default_proposal(struct fix_proposal *p)
{
  memset(p, 0, sizeof(*p));
  strcopy(p->role, "fixsuggest", FIX_ROLE_LEN);
  strcopy(p->target, "parser", FIX_TARGET_LEN);
  p->action = FIX_ACTION_REPORT;                  // safest whitelisted action
  p->severity = 0;
  p->retry_hint = 0;
}

// Build the INITIAL candidate fix proposal for one line. A flagged ("ERROR")
// upstream response yields an aggressive, out-of-range severity guess — an
// unsafe suggestion the kernel verifier is expected to reject. The
// *correction* is not made here; it is driven by the verdict (see
// amend_from_verdict), so this stays a pure function of the response.
static void
build_proposal(struct fix_proposal *p, const char *resp)
{
  memset(p, 0, sizeof(*p));
  strcopy(p->role, "fixsuggest", FIX_ROLE_LEN);   // proposing agent
  strcopy(p->target, "parser", FIX_TARGET_LEN);   // non-protected target
  p->action = FIX_ACTION_REQUEUE;                 // whitelisted action
  p->retry_hint = 0;
  p->severity = quality_bad(resp) ? 9 : 2;        // 9 is out of [0,3]
}

// Correct a rejected proposal according to the kernel's verdict code,
// starting from the last accepted state `base` for any field the verdict
// does not pin. This is the closed loop: the fix the agent resubmits is a
// causal function of *why* the kernel rejected it, not of the attempt count.
static void
amend_from_verdict(struct fix_proposal *p, const struct fix_proposal *base,
                   int rc)
{
  switch (rc) {
  case VERIFY_ERR_RANGE:                           // numeric field out of range
    if (p->severity < FIX_SEVERITY_MIN) p->severity = FIX_SEVERITY_MIN;
    if (p->severity > FIX_SEVERITY_MAX) p->severity = FIX_SEVERITY_MAX;
    if (p->retry_hint < 0) p->retry_hint = 0;
    break;
  case VERIFY_ERR_ACTION:                          // action not whitelisted
    p->action = FIX_ACTION_REQUEUE;
    break;
  case VERIFY_ERR_PROTECTED:                       // destructive vs protected
    p->action = FIX_ACTION_REPORT;                 // de-escalate to safe action
    strcopy(p->target, base->target, FIX_TARGET_LEN);
    break;
  case VERIFY_ERR_FIELD:                           // missing / oversized field
    strcopy(p->role, base->role, FIX_ROLE_LEN);
    strcopy(p->target, base->target, FIX_TARGET_LEN);
    break;
  default:                                         // unknown -> revert to base
    *p = *base;
    break;
  }
}

int
main(void)
{
  char line[256];
  char resp[256];
  char last_resp[256];
  char reason[64];
  struct fix_proposal prop, base;

  setrole("evaluator");

  // Seed the checkpoint slot with a valid, known-good baseline so the first
  // rollback restores real (in-range, whitelisted) state to amend from.
  build_default_proposal(&base);
  checkpoint(&base, sizeof(base));

  while (proxy_readline(0, line, sizeof(line)) > 0) {
    int ok = 0;
    int attempts;
    last_resp[0] = 0;
    for (attempts = 0; attempts < MAX_RETRIES; attempts++) {
      if (proxy_call("evaluator", line, resp, sizeof(resp)) < 0) goto done;
      strcopy(last_resp, resp, sizeof(last_resp));

      if (attempts == 0)
        build_proposal(&prop, resp);     // initial candidate (may be unsafe)
      // on a retry, prop already carries the verdict-driven correction below

      int rc = verifyfix(&prop, reason, sizeof(reason));
      if (rc == VERIFY_OK) {
        checkpoint(&prop, sizeof(prop));   // commit the accepted state
        emit(1, "EVAL_ACCEPT ", line);
        ok = 1;
        break;
      }
      // Rejected by the kernel: report why, roll back to the last accepted
      // state, and amend the proposal per the verdict code (not the counter).
      emit(1, "EVAL_VERIFY_FAIL ", reason);
      restore(&base, sizeof(base));        // last accepted state (now consumed)
      emit(1, "EVAL_ROLLBACK ", line);
      amend_from_verdict(&prop, &base, rc);
      emit(1, "EVAL_RETRY ", line);
    }
    if (ok)
      emit(1, "evaluator:OK:", last_resp);
    else
      emit(1, "evaluator:FAIL:", line);
  }
done:
  write(1, "TRIAGE_DONE\n", 12);
  exit(0);
}
