// Liberal_OS T-22 CLI: prints a single-line JSON snapshot of every
// active proc by invoking the agentstat(2) syscall. Output is routed
// through kernel printf; this wrapper exists so the harness can drive
// the call from the xv6 shell.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  agentstat();
  exit(0);
}
