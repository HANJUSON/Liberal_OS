// Liberal_OS T-35: triage orchestrator. Builds a fork+pipe chain of
// five agent processes:
//
//   stdin(or file) -> parser -> classifier -> rootcause
//                  -> fixsuggest -> evaluator -> stdout
//
// Each stage runs as its own xv6 proc; stages communicate via the
// kernel's pipe(2). Per-stage proxy traffic goes through the dedicated
// /console fd opened by proxy_client.h, which is unaffected by the
// pipe wiring of fd 0 / fd 1 in middle stages.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

static void
die(const char *msg)
{
  printf("triage: %s\n", msg);
  exit(1);
}

// Spawn a stage: dup `in_fd` to stdin and `out_fd` to stdout, close
// every pipe end whose value is given in `close_fds[]` (terminated by
// -1), then exec the binary. Parent returns the child pid.
static int
spawn_stage(const char *prog, int in_fd, int out_fd, int *close_fds)
{
  int pid = fork();
  if (pid < 0) die("fork failed");
  if (pid == 0) {
    if (in_fd != 0) {
      close(0);
      if (dup(in_fd) != 0) die("dup stdin");
    }
    if (out_fd != 1) {
      close(1);
      if (dup(out_fd) != 1) die("dup stdout");
    }
    // Close all pipe fds in the child so EOF propagates correctly.
    for (int i = 0; close_fds[i] != -1; i++) close(close_fds[i]);
    char *argv[] = { (char *)prog, 0 };
    exec((char *)prog, argv);
    die("exec failed");
  }
  return pid;
}

int
main(int argc, char *argv[])
{
  int in_fd = 0;
  if (argc > 1) {
    in_fd = open(argv[1], O_RDONLY);
    if (in_fd < 0) die("cannot open input");
  }

  setrole("triage");

  int p1[2], p2[2], p3[2], p4[2];
  if (pipe(p1) < 0 || pipe(p2) < 0 || pipe(p3) < 0 || pipe(p4) < 0)
    die("pipe failed");

  // Each stage needs its own close-list of every pipe end it should not
  // hold open. Listing the originals + neighbours' ends ensures EOF
  // propagates when the upstream stage closes its writer.
  int close_p[] = { p1[0], p1[1], p2[0], p2[1], p3[0], p3[1], p4[0], p4[1], -1 };

  spawn_stage("parser",     in_fd, p1[1], close_p);
  spawn_stage("classifier", p1[0], p2[1], close_p);
  spawn_stage("rootcause",  p2[0], p3[1], close_p);
  spawn_stage("fixsuggest", p3[0], p4[1], close_p);
  spawn_stage("evaluator",  p4[0], 1,     close_p);

  // Parent: release every pipe end + the file fd we opened so the
  // pipeline isn't kept alive past the children's needs.
  close(p1[0]); close(p1[1]);
  close(p2[0]); close(p2[1]);
  close(p3[0]); close(p3[1]);
  close(p4[0]); close(p4[1]);
  if (in_fd != 0) close(in_fd);

  for (int i = 0; i < 5; i++) wait(0);
  exit(0);
}
