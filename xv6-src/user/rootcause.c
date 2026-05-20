// Liberal_OS T-32: root-cause agent.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "user/proxy_client.h"

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

int
main(void)
{
  char line[256];
  char resp[256];

  setrole("rootcause");
  while (proxy_readline(0, line, sizeof(line)) > 0) {
    if (proxy_call("rootcause", line, resp, sizeof(resp)) < 0) break;
    emit(1, "rootcause:", resp);
  }
  exit(0);
}
