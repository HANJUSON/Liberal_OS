#ifndef PROXY_CLIENT_H
#define PROXY_CLIENT_H

// Liberal_OS T-24: tiny header-only proxy client.
//
// xv6 has no networking, so an xv6 user program talks to the Linux host
// over its console serial. We adopt a line-oriented framing both sides
// can lex with one byte of lookahead:
//
//   request : "PROXY_REQ\t<role>\t<prompt>\n"
//   response: "PROXY_RES\t<result>\n"
//
// Anything else on the wire (shell prompts, AGENT_LOG output, the kernel
// startup banner) is ignored by both peers. Tab is the delimiter so the
// prompt may contain spaces; embedded tabs/newlines in role or prompt
// are caller responsibility.
//
// Header-only so any user program can `#include "user/proxy_client.h"`
// without changes to the Makefile UPROGS / link rules.

#include "kernel/types.h"
#include "user/user.h"

#define PROXY_REQ_TAG "PROXY_REQ"
#define PROXY_RES_TAG "PROXY_RES"

static inline void
_proxy_write_str(const char *s)
{
  // strlen via ulib; write returns -1 only on closed fd which we
  // can't recover from anyway, so propagate by short-write.
  if (s == 0) return;
  write(1, (void *)s, strlen(s));
}

// Emit one framed request line to stdout. Returns 0 on success, -1 on
// invalid arguments (null pointers).
static inline int
proxy_send(const char *role, const char *prompt)
{
  if (role == 0 || prompt == 0) return -1;
  _proxy_write_str(PROXY_REQ_TAG);
  _proxy_write_str("\t");
  _proxy_write_str(role);
  _proxy_write_str("\t");
  _proxy_write_str(prompt);
  _proxy_write_str("\n");
  return 0;
}

// Block until a PROXY_RES line shows up on stdin, copy its <result>
// payload into out[outmax-1] (NUL-terminated), and return the number of
// bytes copied (excluding the NUL). Returns -1 on EOF before a frame
// arrives or on bad arguments.
//
// Non-matching lines (shell echoes, AGENT_LOG lines, our own request
// being echoed by the host's terminal) are silently dropped.
static inline int
proxy_recv(char *out, int outmax)
{
  if (out == 0 || outmax <= 0) return -1;

  // Line buffer; sized to accommodate a reasonable mock/live response.
  char line[512];
  int linelen = 0;

  for (;;) {
    char c;
    int n = read(0, &c, 1);
    if (n <= 0) return -1;          // EOF / error
    if (c != '\n') {
      if (linelen < (int)sizeof(line) - 1) line[linelen++] = c;
      continue;
    }
    line[linelen] = 0;

    // Match "PROXY_RES\t..." prefix.
    if (linelen > (int)strlen(PROXY_RES_TAG) + 1
        && memcmp(line, PROXY_RES_TAG, strlen(PROXY_RES_TAG)) == 0
        && line[strlen(PROXY_RES_TAG)] == '\t') {
      const char *payload = line + strlen(PROXY_RES_TAG) + 1;
      int plen = linelen - (int)strlen(PROXY_RES_TAG) - 1;
      if (plen > outmax - 1) plen = outmax - 1;
      memmove(out, (void *)payload, plen);
      out[plen] = 0;
      return plen;
    }

    // Not our frame — reset and keep listening.
    linelen = 0;
  }
}

// Convenience: send + receive in one call.
static inline int
proxy_call(const char *role, const char *prompt, char *out, int outmax)
{
  if (proxy_send(role, prompt) < 0) return -1;
  return proxy_recv(out, outmax);
}

#endif // PROXY_CLIENT_H
