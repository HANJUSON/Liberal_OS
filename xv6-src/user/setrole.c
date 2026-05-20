// Liberal_OS T-21 CLI: thin wrapper around the setrole(2) syscall so
// the harness can drive a role assignment from the xv6 shell.
//
//   usage: setrole <role>
//   exits: 0 on success (prints SETROLE_OK <role>), 1 on failure.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf("usage: setrole <role>\n");
    exit(1);
  }
  if(setrole(argv[1]) < 0){
    printf("SETROLE_FAIL\n");
    exit(1);
  }
  printf("SETROLE_OK %s\n", argv[1]);
  exit(0);
}
