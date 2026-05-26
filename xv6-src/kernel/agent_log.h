#ifndef AGENT_LOG_H
#define AGENT_LOG_H

// AGENT_LOG — uniform kernel-side log line for agent-related events.
//
// Emits:  [AGENT][<level>] <formatted message>
//
// Tagging every agent-relevant kernel print with this prefix lets the
// harness extract them with a single `grep "\[AGENT\]"` — see
// HARNESS.md §4.7. <level> is a string literal so it inlines into the
// format string (e.g., AGENT_LOG("info", "task %d", 42)).
//
// Kept dependency-free so it works from any kernel context, including
// early boot before proc.h's full definitions are visible. Phase 2
// will add a richer variant that also tags pid/agent_role once the
// proc struct carries those fields (see MASTER_PLAN.md T-20+).

#define AGENT_LOG(level, fmt, ...) \
  printf("[AGENT][" level "] " fmt "\n", ##__VA_ARGS__)

#endif // AGENT_LOG_H
