// Liberal_OS T-34 + T-40/T-41: evaluator agent with bounded retry loop.
//
// Quality rule (mock): a downstream response is considered "bad" if it
// still contains the substring "ERROR" after the full upstream chain
// has stamped it. Each input line is proxy_call()ed up to MAX_RETRIES
// times; every retry attempt emits a tagged "EVAL_RETRY" line so the
// harness/daemon can count them. After MAX_RETRIES attempts that all
// fail the quality check, the line is flagged with "evaluator:FAIL:".
//
// Why bounded local retry instead of upstream pipe-feedback (option A
// in the design choice): the natural feedback topology — evaluator →
// triage demux → parser → ... → evaluator — creates a termination
// cycle (each side waits for the other to close first). Pumping the
// retry decision through the evaluator's own proxy_call channel keeps
// the loop closure trivial and still demonstrates the Supervisor
// pattern: quality verification, retry budget, escalation on
// exhaustion. The "worker re-execution" contract collapses into "call
// the same role twice" — observably equivalent under the mock host
// since each call is an independent host roundtrip.

#include "kernel/types.h"
#include "kernel/stat.h"
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
  // upstream didn't successfully classify/diagnose, so retry.
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

int
main(void)
{
  char line[256];
  char resp[256];
  char last_resp[256];

  setrole("evaluator");
  while (proxy_readline(0, line, sizeof(line)) > 0) {
    int ok = 0;
    int attempts;
    last_resp[0] = 0;
    for (attempts = 0; attempts < MAX_RETRIES; attempts++) {
      if (proxy_call("evaluator", line, resp, sizeof(resp)) < 0) goto done;
      strcopy(last_resp, resp, sizeof(last_resp));
      if (!quality_bad(resp)) { ok = 1; break; }
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
