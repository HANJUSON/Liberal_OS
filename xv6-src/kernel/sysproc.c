#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "vm.h"

// Liberal_OS T-30+: a single kernel-side sleeplock serializing
// proxy_call sequences. With multiple agent procs sharing /console
// for the proxy channel, only one may have an outstanding PROXY_REQ
// at a time; otherwise sibling readers race on consoleread() and
// steal each other's PROXY_RES bytes. Initialized once from main().
struct sleeplock proxy_lock;

void
proxyinit(void)
{
  initsleeplock(&proxy_lock, "proxy");
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
