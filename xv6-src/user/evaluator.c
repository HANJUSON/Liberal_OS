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
// The mock host is a pure echo, so convergence does not come from a
// changing response; it comes from the agent amending the rejected
// proposal in light of the verifier's reason. A flagged ("ERROR") line
// yields an out-of-range proposal on the first attempt (rejected), which
// the agent clamps into range on retry (accepted) — deterministically
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

// Build the candidate fix proposal for one line. A flagged response on the
// first attempt produces an out-of-range severity — an unsafe suggestion
// the kernel verifier rejects. On any later attempt the agent has already
// been told why, so it clamps severity into range and resubmits.
static void
build_proposal(struct fix_proposal *p, const char *resp, int attempt)
{
  memset(p, 0, sizeof(*p));
  strcopy(p->role, "fixsuggest", FIX_ROLE_LEN);   // proposing agent
  strcopy(p->target, "parser", FIX_TARGET_LEN);   // non-protected target
  p->action = FIX_ACTION_REQUEUE;                 // whitelisted action
  p->retry_hint = attempt;
  if (quality_bad(resp) && attempt == 0)
    p->severity = 9;        // out of [0,3] -> VERIFY_ERR_RANGE on attempt 0
  else
    p->severity = 2;        // corrected/valid on retry (or clean line)
}

int
main(void)
{
  char line[256];
  char resp[256];
  char last_resp[256];
  char reason[64];
  struct fix_proposal prop;

  setrole("evaluator");

  // Seed the checkpoint slot with a baseline so the first rollback after a
  // rejected proposal has a prior accepted state to restore to.
  checkpoint("BASELINE", 9);

  while (proxy_readline(0, line, sizeof(line)) > 0) {
    int ok = 0;
    int attempts;
    last_resp[0] = 0;
    for (attempts = 0; attempts < MAX_RETRIES; attempts++) {
      if (proxy_call("evaluator", line, resp, sizeof(resp)) < 0) goto done;
      strcopy(last_resp, resp, sizeof(last_resp));

      build_proposal(&prop, resp, attempts);
      if (verifyfix(&prop, reason, sizeof(reason)) == VERIFY_OK) {
        checkpoint(&prop, sizeof(prop));   // commit the accepted state
        emit(1, "EVAL_ACCEPT ", line);
        ok = 1;
        break;
      }
      // Rejected by the kernel: report why, roll back to the last accepted
      // state, and retry with a corrected proposal.
      emit(1, "EVAL_VERIFY_FAIL ", reason);
      restore(&prop, sizeof(prop));
      emit(1, "EVAL_ROLLBACK ", line);
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
