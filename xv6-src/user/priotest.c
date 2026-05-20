// Liberal_OS T-52: priority scheduling effect probe (minimal).
//
// Forks N CPU-bound children with distinct priorities, prints their
// completion ticks in finish order. Output is line-by-line so the
// harness can scrape it from serial without needing a report pipe.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define N_CHILDREN 6
#define ITERS      4000000

static void
emit_done(int idx, int prio, int t)
{
  // "DONE i=<n> prio=<p> t=<ticks>\n" in a single write.
  char buf[64];
  int p = 0, n;
  const char *pfx = "DONE i=";
  for (n = 0; pfx[n]; n++) buf[p++] = pfx[n];
  buf[p++] = '0' + (idx % 10);
  const char *prfx = " prio=";
  for (n = 0; prfx[n]; n++) buf[p++] = prfx[n];
  if (prio == 0) buf[p++] = '0';
  else {
    char tmp[8]; int k = 0; int pp = prio;
    while (pp > 0) { tmp[k++] = '0' + (pp % 10); pp /= 10; }
    while (k--) buf[p++] = tmp[k];
  }
  const char *tfx = " t=";
  for (n = 0; tfx[n]; n++) buf[p++] = tfx[n];
  if (t == 0) buf[p++] = '0';
  else {
    char tmp[16]; int k = 0; int tt = t;
    while (tt > 0) { tmp[k++] = '0' + (tt % 10); tt /= 10; }
    while (k--) buf[p++] = tmp[k];
  }
  buf[p++] = '\n';
  write(1, buf, p);
}

int
main(void)
{
  int t0 = uptime();
  for (int i = 0; i < N_CHILDREN; i++) {
    int pid = fork();
    if (pid < 0) { write(1, "FAIL fork\n", 10); exit(1); }
    if (pid == 0) {
      int prio = N_CHILDREN - 1 - i;   // i=0 → highest
      setprio(prio);
      volatile uint64 sum = 0;
      for (int j = 0; j < ITERS; j++) sum += j;
      emit_done(i, prio, uptime() - t0);
      (void)sum;
      exit(0);
    }
  }
  for (int i = 0; i < N_CHILDREN; i++) wait(0);
  write(1, "PRIOTEST_DONE\n", 14);
  exit(0);
}
