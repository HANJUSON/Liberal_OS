#ifndef PROXY_CLIENT_H
#define PROXY_CLIENT_H

// Liberal_OS T-24/T-30: header-only proxy client.
//
// xv6 has no networking, so an xv6 user program talks to the Linux host
// over the console serial. We adopt a line-oriented framing both sides
// can lex with one byte of lookahead:
//
//   request : "PROXY_REQ\t<id>\t<role>\t<prompt>\n"
//   response: "PROXY_RES\t<id>\t<result>\n"
//
// <id> is the caller's pid. Several agent procs share the console for
// proxy traffic in a pipe chain (T-30+), so the host echoes the id back
// and each caller drops responses whose id doesn't match its own. The
// host serializes REQ→RES in arrival order; the id is the demux key,
// not an ordering hint.
//
// Anything else on the wire (shell prompts, AGENT_LOG output, the kernel
// startup banner) is ignored by both peers. Tab is the delimiter so the
// prompt may contain spaces; embedded tabs/newlines in role or prompt
// are caller responsibility.
//
// **Channel separation (T-30)**: opens "/console" lazily on first use and
// caches the resulting fd. Agents in a pipe chain — where fd 0 / fd 1
// are wired to neighbours — can still proxy_call because read/write here
// go through a dedicated console fd that is unaffected by pipe wiring.
// The console driver does multiplex one input stream so the host daemon
// is the one that distinguishes PROXY_RES frames from interactive keys
// during automated runs.

#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define PROXY_REQ_TAG "PROXY_REQ"
#define PROXY_RES_TAG "PROXY_RES"

static int _proxy_fd_cached = -1;

static inline int
_proxy_fd(void)
{
  if (_proxy_fd_cached >= 0) return _proxy_fd_cached;
  _proxy_fd_cached = open("console", O_RDWR);
  return _proxy_fd_cached;
}

static inline void
_proxy_write_str(int fd, const char *s)
{
  if (s == 0) return;
  write(fd, (void *)s, strlen(s));
}

// Append the decimal form of `v` to buf[*pos..], bumping *pos. Caller
// is responsible for ensuring max space — keep in sync with proxy_send's
// stack buffer size.
static inline void
_proxy_append_int(char *buf, int *pos, int v)
{
  if (v < 0) { buf[(*pos)++] = '-'; v = -v; }
  char tmp[12];
  int n = 0;
  if (v == 0) tmp[n++] = '0';
  else { while (v > 0 && n < (int)sizeof(tmp)) { tmp[n++] = '0' + (v % 10); v /= 10; } }
  while (n--) buf[(*pos)++] = tmp[n];
}

static inline void
_proxy_append_str(char *buf, int *pos, int max, const char *s)
{
  while (s && *s && *pos < max - 1) buf[(*pos)++] = *s++;
}

// Emit one framed request line on the dedicated console fd, tagging
// the caller's pid as request id. The whole line is built in a stack
// buffer and pushed in a single write() so kernel printf traffic
// (AGENT_LOG from sibling syscalls, etc.) cannot interleave at byte
// granularity. Returns 0 on success, -1 on invalid arguments.
static inline int
proxy_send(const char *role, const char *prompt)
{
  int fd = _proxy_fd();
  if (fd < 0 || role == 0 || prompt == 0) return -1;
  char buf[640];
  int pos = 0;
  _proxy_append_str(buf, &pos, sizeof(buf), PROXY_REQ_TAG);
  buf[pos++] = '\t';
  _proxy_append_int(buf, &pos, getpid());
  buf[pos++] = '\t';
  _proxy_append_str(buf, &pos, sizeof(buf), role);
  buf[pos++] = '\t';
  _proxy_append_str(buf, &pos, sizeof(buf), prompt);
  buf[pos++] = '\n';
  write(fd, buf, pos);
  return 0;
}

// Helper: parse a non-negative decimal integer from `s` for at most
// `max` bytes. Returns 1 on success and sets *end to the byte after the
// last digit. Returns 0 on no digits.
static inline int
_proxy_parse_int(const char *s, int max, int *out, int *consumed)
{
  int v = 0, i = 0;
  while (i < max && s[i] >= '0' && s[i] <= '9') {
    v = v * 10 + (s[i] - '0');
    i++;
  }
  if (i == 0) return 0;
  *out = v;
  *consumed = i;
  return 1;
}

// Block until a PROXY_RES line matching `our` id shows up on the
// dedicated console fd, copy its <result> payload into out[outmax-1]
// (NUL-terminated), and return the number of bytes copied (excluding
// the NUL). Frames addressed to a different id are silently dropped —
// the sibling agent who owns that id will pick them up. Returns -1 on
// EOF before a frame arrives or on bad arguments.
static inline int
proxy_recv(char *out, int outmax)
{
  int fd = _proxy_fd();
  if (fd < 0 || out == 0 || outmax <= 0) return -1;
  int our_id = getpid();

  char line[512];
  int linelen = 0;
  int taglen = (int)strlen(PROXY_RES_TAG);

  for (;;) {
    char c;
    int n = read(fd, &c, 1);
    if (n <= 0) return -1;
    if (c != '\n') {
      if (linelen < (int)sizeof(line) - 1) line[linelen++] = c;
      continue;
    }
    line[linelen] = 0;

    // Expect "PROXY_RES\t<id>\t<payload>".
    if (linelen > taglen + 1
        && memcmp(line, PROXY_RES_TAG, taglen) == 0
        && line[taglen] == '\t') {
      int id, idlen;
      if (_proxy_parse_int(line + taglen + 1, linelen - taglen - 1, &id, &idlen)
          && line[taglen + 1 + idlen] == '\t'
          && id == our_id) {
        const char *payload = line + taglen + 1 + idlen + 1;
        int plen = linelen - taglen - 1 - idlen - 1;
        if (plen > outmax - 1) plen = outmax - 1;
        memmove(out, (void *)payload, plen);
        out[plen] = 0;
        return plen;
      }
    }
    linelen = 0;
  }
}

// Atomic send+recv: hold the kernel-side proxy sleeplock so a sibling
// agent can't read our PROXY_RES bytes from /console between our send
// and recv. T-30+: pipelined agents share /console; without this guard,
// consoleread() races corrupt the demux. The lock-free proxy_send /
// proxy_recv primitives are still exposed for callers that want to
// stage requests outside the critical section.
static inline int
proxy_call(const char *role, const char *prompt, char *out, int outmax)
{
  int r;
  proxylock();
  if (proxy_send(role, prompt) < 0) { proxyunlock(); return -1; }
  r = proxy_recv(out, outmax);
  proxyunlock();
  return r;
}

// Optional helper for line-oriented agents: read one '\n'-terminated
// line from `fd` into buf[max-1] (NUL-terminated). Returns line length
// (excluding NUL) on success, 0 on EOF with empty buffer, -1 on error.
static inline int
proxy_readline(int fd, char *buf, int max)
{
  if (buf == 0 || max <= 1) return -1;
  int n = 0;
  for (;;) {
    char c;
    int r = read(fd, &c, 1);
    if (r < 0) return -1;
    if (r == 0) {
      buf[n] = 0;
      return n;
    }
    if (c == '\n') {
      buf[n] = 0;
      return n;
    }
    if (n < max - 1) buf[n++] = c;
  }
}

#endif // PROXY_CLIENT_H
