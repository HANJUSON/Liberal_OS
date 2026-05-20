// Liberal_OS T-20 functional smoke: confirm fork+pipe still work after
// the proc struct grew agent_role/priority/agent_state fields.
//
// Strict inheritance assertion (parent.agent_role == child.agent_role)
// requires a userspace reader for the new fields, which arrives with
// T-22 (agentstat). For now this exercises the fork path so the new
// memset/copy logic in proc.c is at least executed under autotest.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  int p[2];
  char buf[8];
  int pid, status;

  if(pipe(p) < 0){
    printf("PROC_FIELDS_FAIL pipe\n");
    exit(1);
  }
  pid = fork();
  if(pid < 0){
    printf("PROC_FIELDS_FAIL fork\n");
    exit(1);
  }
  if(pid == 0){
    close(p[0]);
    write(p[1], "ok", 2);
    close(p[1]);
    exit(0);
  }
  close(p[1]);
  if(read(p[0], buf, 2) != 2){
    printf("PROC_FIELDS_FAIL read\n");
    exit(1);
  }
  close(p[0]);
  if(wait(&status) != pid || status != 0){
    printf("PROC_FIELDS_FAIL wait\n");
    exit(1);
  }
  printf("PROC_FIELDS_OK\n");
  exit(0);
}
