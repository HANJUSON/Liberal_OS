// Liberal_OS T-24 verify: send a PROXY_REQ frame and print the response
// so the host-side mock daemon (T-25) can complete the round trip.
//
// Output token PROXY_TEST_OK appears only when the response payload
// matches the prompt we sent (mock daemon does pure echo).

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "user/proxy_client.h"

int
main(int argc, char *argv[])
{
  const char *role = argc > 1 ? argv[1] : "echo";
  const char *prompt = argc > 2 ? argv[2] : "hello";
  char buf[256];

  int n = proxy_call(role, prompt, buf, sizeof(buf));
  if (n < 0) {
    printf("PROXY_TEST_FAIL recv\n");
    exit(1);
  }
  printf("PROXY_TEST_RESP %s\n", buf);
  if (strcmp(buf, prompt) == 0) printf("PROXY_TEST_OK\n");
  else                          printf("PROXY_TEST_DIFF\n");
  exit(0);
}
