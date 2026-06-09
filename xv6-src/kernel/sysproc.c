#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "vm.h"
#include "verifier.h"
#include "cache.h"

// Liberal_OS T-30+: a single kernel-side sleeplock serializing
// proxy_call sequences. With multiple agent procs sharing /console
// for the proxy channel, only one may have an outstanding PROXY_REQ
// at a time; otherwise sibling readers race on consoleread() and
// steal each other's PROXY_RES bytes. Initialized once from main().
struct sleeplock proxy_lock;

// Liberal_OS Pattern A (T-82): a single fixed-size kernel checkpoint slot
// holding the last accepted agent state. sys_checkpoint saves a user blob
// into it; sys_restore copies it back so a failed verify can roll back to
// the last good state. Kept global (not a per-proc field) so proc.c and
// the scheduler stay untouched (HUMAN GATE).
#define CKPT_MAX 256
struct {
  struct spinlock lock;
  int  len;              // 0 = nothing checkpointed yet
  char buf[CKPT_MAX];
} ckpt;

void
proxyinit(void)
{
  initsleeplock(&proxy_lock, "proxy");
  initlock(&ckpt.lock, "ckpt");
  ckpt.len = 0;
  cacheinit();
}

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// Liberal_OS T-21: set the calling proc's agent_role.
// Accepts a string up to 15 chars (proc.agent_role is 16 with NUL). Empty
// strings and oversize strings return -1 without touching state. Emits an
// AGENT_LOG line so the harness can observe the change before T-22's
// agentstat reader lands.
uint64
sys_setrole(void)
{
  char buf[16];
  struct proc *p = myproc();
  int n;

  n = argstr(0, buf, sizeof(buf));
  if(n <= 1)          // argstr returns length including NUL; <=1 means empty
    return -1;

  safestrcpy(p->agent_role, buf, sizeof(p->agent_role));
  // T-21 originally emitted an AGENT_LOG here for observability, but
  // multi-process pipelines (T-30+) showed that kernel printfs can
  // interleave with concurrent user writes at byte granularity on the
  // shared console UART, corrupting PROXY_REQ frames. Until the UART
  // driver acquires a per-write lock, agent-role transitions are
  // observable via agentstat(2) instead of inline logs.
  return 0;
}

// Liberal_OS T-22: dump a one-line JSON snapshot of every active proc.
// Output goes straight to the console via printf (which xv6 routes to
// both serial and the calling proc's stdout when run from a shell).
// Locking note: matches procdump's no-lock walk — caller is expected
// to be running interactively, racing readers tolerate slight skew.
uint64
sys_agentstat(void)
{
  static char *states[] = {
    [UNUSED]   "unused",
    [USED]     "used",
    [SLEEPING] "sleep",
    [RUNNABLE] "runble",
    [RUNNING]  "run",
    [ZOMBIE]   "zombie",
  };
  struct proc *p;
  int first = 1;
  char *st;

  printf("[");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      st = states[p->state];
    else
      st = "?";
    if(!first)
      printf(",");
    first = 0;
    printf("{\"pid\":%d,\"name\":\"%s\",\"role\":\"%s\",\"prio\":%d,\"st\":\"%s\"}",
           p->pid, p->name, p->agent_role, p->priority, st);
  }
  printf("]\n");
  return 0;
}

// Liberal_OS T-52: set the calling proc's scheduling priority.
// Higher values get scheduler() preference; default is 0. Clamped to
// [-20, 19] in unix-tradition so callers can't pin the scheduler at
// MAX_INT. Returns the previous priority on success.
uint64
sys_setprio(void)
{
  int prio, prev;
  struct proc *p = myproc();
  argint(0, &prio);
  if (prio < -20) prio = -20;
  if (prio > 19)  prio = 19;
  prev = p->priority;
  p->priority = prio;
  return prev;
}

uint64
sys_proxylock(void)
{
  acquiresleep(&proxy_lock);
  return 0;
}

uint64
sys_proxyunlock(void)
{
  if (holdingsleep(&proxy_lock))
    releasesleep(&proxy_lock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Pattern A (T-81): resolve the proposal's target against the proc table
// so the pure verifier never walks it. Sets target_pid (or -1) and a
// target_protected flag when the named process is a protected one.
static void
fill_snapshot(struct fix_proposal *p, struct sys_snapshot *s)
{
  struct proc *pp;
  int live = 0;

  s->target_pid = -1;
  s->target_protected = 0;
  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->state == UNUSED)
      continue;
    live++;
    // Match the target against either the OS process name or agent role.
    if(strncmp(pp->name, p->target, FIX_TARGET_LEN) == 0 ||
       strncmp(pp->agent_role, p->target, FIX_TARGET_LEN) == 0){
      s->target_pid = pp->pid;
      if(strncmp(pp->name, "init", 16) == 0 ||
         strncmp(pp->name, "sh", 16) == 0 ||
         strncmp(pp->agent_role, "evaluator", 16) == 0)
        s->target_protected = 1;
    }
  }
  s->nproc = live;
}

// sys_verifyfix(struct fix_proposal *p, char *reason, int rlen)
//   arg0: user pointer to a fix_proposal
//   arg1: user pointer to a reason buffer (may be 0)
//   arg2: length of that buffer
// Returns VERIFY_OK (0) or a negative VERIFY_ERR_* code; -100 on a bad
// argument (kept distinct from the verifier's -1..-4 codes).
uint64
sys_verifyfix(void)
{
  uint64 paddr, raddr;
  int rlen, rc;
  struct fix_proposal p;
  struct sys_snapshot snap;
  char reason[64];

  argaddr(0, &paddr);
  argaddr(1, &raddr);
  argint(2, &rlen);

  if(copyin(myproc()->pagetable, (char*)&p, paddr, sizeof(p)) < 0)
    return -100;

  fill_snapshot(&p, &snap);
  rc = verify_fix(&p, &snap, reason, sizeof(reason));

  if(raddr != 0 && rlen > 0){
    int n = rlen < (int)sizeof(reason) ? rlen : (int)sizeof(reason);
    if(copyout(myproc()->pagetable, raddr, reason, n) < 0)
      return -100;
  }
  return (uint64)(long)rc;   // sign-extend so user int sees -1..-4
}

// sys_checkpoint(void *src, int len): save up to CKPT_MAX bytes of user
// state into the kernel checkpoint slot. Returns bytes stored, or -1 on a
// bad length / fault.
uint64
sys_checkpoint(void)
{
  uint64 src;
  int len;
  char tmp[CKPT_MAX];

  argaddr(0, &src);
  argint(1, &len);
  if(len < 0 || len > CKPT_MAX)
    return -1;
  if(len > 0 && copyin(myproc()->pagetable, tmp, src, len) < 0)
    return -1;

  acquire(&ckpt.lock);
  if(len > 0)
    memmove(ckpt.buf, tmp, len);
  ckpt.len = len;
  release(&ckpt.lock);
  return len;
}

// sys_restore(void *dst, int len): copy the checkpointed state back to the
// user buffer (truncated to len). Returns bytes restored; 0 means nothing
// has been checkpointed; -1 on fault.
uint64
sys_restore(void)
{
  uint64 dst;
  int len, n;
  char tmp[CKPT_MAX];

  argaddr(0, &dst);
  argint(1, &len);
  if(len < 0)
    return -1;

  acquire(&ckpt.lock);
  n = ckpt.len;
  if(n > len)
    n = len;
  if(n > 0)
    memmove(tmp, ckpt.buf, n);
  release(&ckpt.lock);

  if(n > 0 && copyout(myproc()->pagetable, dst, tmp, n) < 0)
    return -1;
  return n;
}

// sys_cacheget(const char *role, const char *prompt, char *out, int outmax)
// Returns 1 on hit (value copied to out), 0 on miss, -1 on a bad argument.
uint64
sys_cacheget(void)
{
  char role[16], prompt[256], out[CACHE_VALLEN];
  uint64 uout;
  int outmax, hit;

  if(argstr(0, role, sizeof(role)) < 0)
    return -1;
  if(argstr(1, prompt, sizeof(prompt)) < 0)
    return -1;
  argaddr(2, &uout);
  argint(3, &outmax);

  hit = cache_get(role, prompt, out, sizeof(out));
  if(hit && uout != 0 && outmax > 0){
    int n = outmax < (int)sizeof(out) ? outmax : (int)sizeof(out);
    if(copyout(myproc()->pagetable, uout, out, n) < 0)
      return -1;
  }
  return hit;
}

// sys_cacheset(const char *role, const char *prompt, const char *val)
// Returns 1 on store, 0 if the table is full, -1 on a bad argument.
uint64
sys_cacheset(void)
{
  char role[16], prompt[256], val[CACHE_VALLEN];

  if(argstr(0, role, sizeof(role)) < 0)
    return -1;
  if(argstr(1, prompt, sizeof(prompt)) < 0)
    return -1;
  if(argstr(2, val, sizeof(val)) < 0)
    return -1;
  return cache_set(role, prompt, val);
}

// sys_cacheclear(int disk): clear the RAM cache; disk != 0 also truncates
// the /cache.bin overlay. Returns 0.
uint64
sys_cacheclear(void)
{
  int disk;
  argint(0, &disk);
  cache_clear(disk);
  return 0;
}
