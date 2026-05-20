// Liberal_OS smoke test: exercise fork/wait/pipe end-to-end so the
// harness (tests/autotest.sh) can grep a single PASS line from serial.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
fail(const char *msg)
{
  printf("SMOKE_TEST_FAIL %s\n", msg);
  exit(1);
}

int
main(void)
{
  int p[2];
  char buf[8];
  int pid, status;

  if(pipe(p) < 0)
    fail("pipe");

  pid = fork();
  if(pid < 0)
    fail("fork");

  if(pid == 0){
    // child: write a 5-byte token, then exit.
    close(p[0]);
    if(write(p[1], "hello", 5) != 5)
      fail("child_write");
    close(p[1]);
    exit(0);
  }

  // parent: read the token and reap the child.
  close(p[1]);
  if(read(p[0], buf, 5) != 5)
    fail("parent_read");
  close(p[0]);
  buf[5] = 0;
  if(strcmp(buf, "hello") != 0)
    fail("payload_mismatch");

  if(wait(&status) != pid)
    fail("wait_pid");
  if(status != 0)
    fail("child_status");

  printf("SMOKE_TEST_PASS\n");
  exit(0);
}
