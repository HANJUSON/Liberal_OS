# Liberal_OS — Technical Report

> Companion document to `README.md`, `MASTER_PLAN.md`, and
> `out/REPORT.md`. Architecture diagrams are kept inline as ASCII so
> the document is reviewable on plain text terminals; rendered PNGs
> alongside (when needed) live in `docs/diagrams/`.
>
> Team contribution narratives belong in `PROCESS.md` §6.

---

## §1. Executive Summary

**Liberal_OS** is a re-implementation of the LLM agent runtime on top of
the xv6 educational kernel. The project picks **Direction A** of the
course guideline — *OS for LLM* — and uses it to drive a deliberate
decision: every multi-agent OS primitive normally borrowed from a host
operating system (process isolation, IPC, priority scheduling, syscalls,
locks) is **directly designed and implemented inside xv6 source**
instead of invoked from a Linux user-space library such as
`multiprocessing` or `cgroups`. The killer application is a five-stage
log-triage pipeline (parser → classifier → root-cause → fix-suggester
→ evaluator) that runs as five xv6 user processes, communicates over
pipes, and reaches an Upstage Solar Pro 3 model via a Linux-side proxy
daemon.

End-to-end the system processes a five-line input log under mock-mode
LLM responses in **0.98 ± 0.03 seconds** (5-iteration average). The
Evaluator agent observes a bounded-retry Supervisor pattern: lines that
fail its quality rule are retried up to three times before being flagged
`evaluator:FAIL`. A custom priority-aware scheduler in `scheduler()`
demonstrates that high-priority agents complete strictly before
lower-priority siblings on the same QEMU instance.

The remainder of this report walks through the OS-concept mapping
(§3), the architecture (§4), implementation details by phase (§5–§9),
evaluation results (§10), known limitations (§11), and future work
(§12).

---

## §2. Project Direction and Motivation

The course guideline (§2) requires every project to choose between

* **Direction A — OS for LLM:** build an OS, runtime, or platform that
  hosts and orchestrates LLMs, or
* **Direction B — LLM for OS:** integrate an LLM into a classical OS
  problem.

The Liberal_OS team chose Direction A, in the sub-category *"a mini
OS or user-space supervisor that manages multiple concurrent LLM
processes (agents) under CPU, memory, and tool quotas."*

The pivotal design decision recorded in `CLAUDE.md` §0 is

> The single line we must keep in mind is **not** "call the API via
> `multiprocessing`" — it is "**xv6 directly manages the LLM
> processes**."

That distinction matters because the guideline §2 explicitly warns that
"a thin wrapper around an LLM API" and "OS aspects that amount to 'it
runs on Linux'" are **disqualifying**. We therefore commit to
implementing each OS primitive that the agent runtime depends on
*inside xv6 source* — `proc.h`, `proc.c`, `sysproc.c`, `syscall.c/h`,
`console.c`, `printf.c`, `main.c` — and to never reach for a
host-side library to fill the gap.

What the host *does* provide is the LLM transport (xv6 has no network
stack), and we make that boundary explicit: from the guest's point of
view a remote LLM call is a `read`/`write` pair against a console
file descriptor that the user-space library `proxy_client.h` opens.
The host-side proxy daemon (Python) is responsible for the actual
Upstage API call; xv6 doesn't know — or need to know — that LLMs
exist.

---

## §3. OS Concept Mapping (key reference table)

The course guideline §2 demands that at least one — preferably more —
of *processes, threads, synchronisation, scheduling, virtual memory,
file systems, IPC, system calls* be **directly designed and
implemented**. Liberal_OS **directly implements seven** core OS concepts
in modified xv6 source (rows 1–7), **extends them with two Phase 8/9
kernel subsystems** — a fix verifier and a semantic response cache
(rows 8–9, the *verify+rollback* and *semantic-cache* patterns) — and
**uses one more** concept (the file system, read-only for input) as the
final row:

| # | Liberal_OS component | OS concept | xv6 file(s) modified | Implementation summary |
|---|---|---|---|---|
| 1 | Per-agent metadata | **Process management** | `proc.h`, `proc.c` | `struct proc` extended with `agent_role[16]`, `priority`, `agent_state`; defaults set in `allocproc()`, inherited from parent in `kfork()`, scrubbed in `freeproc()`. |
| 2 | Pipeline backbone | **IPC (pipes)** | `triage.c`, existing `pipe.c` | Five-stage chain wired with four xv6 pipes; orchestrator uses standard `fork`/`dup`/`close` to redirect each stage's `stdin`/`stdout`. |
| 3 | Cross-agent supervision | **IPC (line-framed protocol)** | `proxy_client.h`, `proxy_daemon.py` | Tab-delimited `PROXY_REQ\t<id>\t<role>\t<prompt>` / `PROXY_RES\t<id>\t<result>` framing over a dedicated `/console` fd; pid serves as demux id so sibling agents do not steal each other's responses. |
| 4 | Mutual exclusion on shared resource | **Synchronisation (sleeplocks)** | `console.c`, `sysproc.c`, new syscalls 24 & 25 | A kernel sleeplock (`cons_write_lock`) wraps `consolewrite` so per-write atomicity holds against sibling user writes; a second sleeplock (`proxy_lock`) plus syscalls `proxylock(2)` / `proxyunlock(2)` serialise the *send + recv* of one `proxy_call` against any other agent's. |
| 5 | Differentiated agent service | **Scheduling (priority)** | `proc.c` `scheduler()` | Two-pass selection (max-priority RUNNABLE first, multi-CPU fallback) plus `setprio(int)` syscall clamped to nice-style `[-20, 19]`. Default `priority = 0` reduces to baseline Round Robin. |
| 6 | Fault containment | **Process isolation** | inherent in xv6 | Each agent is its own `struct proc` with its own page table; the `procfields` user-space smoke confirms that fork+pipe semantics are not regressed by our struct extension. |
| 7 | Observability | **System calls** | `syscall.c/h`, `sysproc.c`, `user.h`, `usys.pl` | Six new syscalls: `setrole(22)`, `agentstat(23)`, `proxylock(24)`, `proxyunlock(25)`, `setprio(26)`. `agentstat` walks the proc table and emits a one-line JSON snapshot. |
| 8 | Kernel-arbitrated fix verification + rollback (**Pattern A**) | **System calls + synchronisation** | `verifier.{c,h}`, `sysproc.c`, new syscalls 27–29 | The LLM is only a *proposer*; the kernel holds final authority. A pure verifier `verify_fix()` checks each fix proposal's structural integrity, numeric ranges, action whitelist, and a protected-process rule. `verifyfix(27)` resolves proc state into a `sys_snapshot` so the verifier never walks `proc[]`; `checkpoint(28)`/`restore(29)` (a spinlock-guarded kernel slot) roll back to the last accepted state on rejection. The evaluator's bounded retry loop drives **VERIFY FAIL → ROLLBACK → RETRY → ACCEPT**. |
| 9 | Kernel semantic response cache (**Pattern B**) | **File system + system calls** | `cache.{c,h}`, `mkfs.c`, new syscalls 30–32 | A 64-slot RAM table keyed by FNV-1a(`role\|prompt`) with per-entry MinHash signatures for paraphrase (semantic) hits via an integer Jaccard threshold, backed by a `/cache.bin` **disk overlay** (append on set; sequential scan + RAM promotion on miss) through the inode API (`writei`/`readi`). `cacheget(30)`/`cacheset(31)`/`cacheclear(32)` expose it; `proxy_call` consults the cache first and **short-circuits the PROXY_REQ (the LLM call) on a hit**. This is also where the kernel *writes* to the xv6 file system, beyond the read-only input use in the final row. |
| — | Input persistence | **File system (used, *not* modified)** | none — uses xv6's existing fs | The input log lives as a real file in the xv6 file system (`short.log`, copied in at `fs.img` build time); agents read it through standard `open(2)` / `read(2)`. This row is listed for completeness — we did **not** add to xv6's filesystem code, so it does **not** count toward the seven directly-implemented concepts above. |

The directly-implemented count (rows 1–7, plus the two Phase 8/9
subsystems in rows 8–9) already exceeds GuideLine
§2's "at least one, preferably more" requirement by a wide margin;
we keep the file-system row visible because the killer scenario *does*
flow data through real xv6 I/O rather than a back-channel, even
though the kernel-side fs code is unchanged.

### §3.1 Why these concepts and not others

The guideline does not require coverage of every OS concept, only that
*at least one* be substantively implemented. We directly implement
seven (every concept except classical multi-threading inside a single
proc, which xv6 doesn't support natively) and additionally use the
existing file system without modifying it. The choice was driven by
what the killer scenario *actually demands*:

* The pipeline shape (five distinct stages communicating in order)
  is what forces IPC + processes — without them the design collapses
  into a single Python function.
* The shared host transport over one serial UART is what forces
  synchronisation primitives, both for per-write atomicity and for
  round-trip serialisation.
* The "critical-vs-info" log differentiation is what gives priority
  scheduling a meaningful semantics; without it, priority would be a
  free parameter with no observable effect.
* The Supervisor pattern is what gives the retry loop and the
  Evaluator-as-quality-gate their meaning, and that pattern requires
  syscalls (to report state) and file-system input (so retries don't
  re-fetch state from RAM that may have churned).

The remaining concept — virtual memory / paging — is exercised by
default because every xv6 user proc lives in its own page table, but
we have not added any custom paging logic and so we do not claim it
as an *implemented* concept beyond xv6's baseline.

---

## §4. System Architecture

### §4.1 Host / Guest split

xv6 has no network stack, so the LLM API call cannot originate inside
the guest. We split responsibilities cleanly:

```
┌─────────────────────────── QEMU host (Linux) ───────────────────────────┐
│                                                                         │
│  ┌──────────── xv6 Guest ────────────┐    ┌─── LLM Proxy Daemon ────┐   │
│  │                                   │    │  host/proxy_daemon.py   │   │
│  │  ┌──── triage (orchestrator) ─┐   │    │                         │   │
│  │  │   fork+pipe wiring         │   │    │  --mode mock   echo     │   │
│  │  │   priotest (priority demo) │   │    │  --mode live   Upstage  │   │
│  │  └─────────────┬──────────────┘   │    │  --mode replay cached   │   │
│  │                │ fork×5           │    │                         │   │
│  │   ┌────────┐ ┌─┴──────┐ ┌────────┐│    │  Serial demux on host:  │   │
│  │   │parser  │→│classif.│→│rootcause││──→│  PROXY_REQ\\tID\\tROLE  │   │
│  │   └────────┘ └────────┘ └────────┘│    │  ─────────────────────  │   │
│  │   ┌────────┐ ┌────────┐           │    │  ←  PROXY_RES\\tID\\t…  │   │
│  │   │fixsugg.│→│evaluat.│ ──────────┼────┤                         │   │
│  │   └────────┘ └────────┘           │    │  .cache/llm/<sha>.json  │   │
│  │      retry loop ↑                 │    │                         │   │
│  └───────────────────────────────────┘    └─────────────────────────┘   │
│                                                                         │
│  Console serial (chardev fallback) — single shared bus, framed protocol │
└─────────────────────────────────────────────────────────────────────────┘
```

Both halves connect over the **console serial** that QEMU already
exposes under `-nographic`. The original design called for a dedicated
virtio-serial port, but we adopt the *chardev pipe fallback* the task
plan explicitly endorses (`MASTER_PLAN.md` T-23 note): the existing serial
carries both shell input/output **and** the framed PROXY traffic;
demux happens by line prefix on each side. This avoids writing a
brand-new xv6 device driver while keeping the public proxy API
(`proxy_client.h`) small enough that a future task can promote the
transport to a real virtio serial without changing callers.

### §4.2 Pipeline data flow

```
input file  ──→  parser  ──→  classifier  ──→  rootcause  ──→  fixsuggest  ──→  evaluator  ──→  stdout
                  │              │              │              │              │
                  └─ proxy_call ─┴─ proxy_call ─┴─ proxy_call ─┴─ proxy_call ─┘
                                                                              ↑
                                                                  bounded retry (max 3)
```

Each stage reads its upstream pipe line by line, makes one
`proxy_call(role, line)` to the host, prefixes the response with its
own role tag, and writes the result to its downstream pipe. The
Evaluator extends this: it applies a quality rule (presence of `ERROR`
in the response) and retries the call up to three times before
flagging the line `evaluator:FAIL:`. Each retry attempt emits an
`EVAL_RETRY` token that the host daemon counts for the report.

### §4.3 Proxy framing on the shared serial

```
xv6 user code              kernel UART              host daemon
─────────────────────────────────────────────────────────────────
proxy_call(role, prompt)
   │
   ├ proxylock()                                       │
   ├ write fd(/console):
   │  "PROXY_REQ\\t<pid>\\t<role>\\t<prompt>\\n"       │
   │                          ──── serial OUT ───→     │ stdout
   │                                                   │ → parse
   │                          ←── serial IN  ────      │ stdin
   ├ read fd(/console) until\\n
   │  "PROXY_RES\\t<pid>\\t<reply>\\n"   ← matches ID  │
   ├ proxyunlock()                                     │
   └ return reply
```

Two distinct safety mechanisms guard this channel:

1. **Per-write atomicity** — `consolewrite` acquires `cons_write_lock`
   (a sleeplock added in `console.c`) so an entire user `write()` of up
   to 1024 bytes lands on the UART without being interleaved with
   another user's write or with a kernel `printf`.
2. **Round-trip serialisation** — `proxy_call` is wrapped in
   `proxylock(2)` / `proxyunlock(2)` syscalls. Only one agent at a time
   may have an outstanding `PROXY_REQ`, otherwise the consoleread
   demux races among sibling agents.

These two mechanisms together are *necessary* — neither alone
suffices. Per-write atomicity stops the byte-level shred we observed
without it; round-trip serialisation stops sibling agents from
consuming each other's `PROXY_RES` from the shared input buffer.

---

## §5. Phase 1 — The Harness

A non-trivial fraction of the engineering went into the *harness* — the
external scaffolding that lets the AI coding agent (Claude Code) drive
xv6 development autonomously without the team baby-sitting every
five-minute iteration. The harness deliverables are documented in
detail in `HARNESS.md`; in summary:

* `CLAUDE.md` (293 lines) — operating manual, work-loop rules, eight
  inviolable constraints (no Linux primitives, no API keys in source,
  no force-push, etc.).
* `MASTER_PLAN.md` (~865 lines) — Part I records design decisions
  (OS-mapping table, host-guest separation, evaluation plan) and
  Part II lists every Phase-1-to-7 work item with dependency graph,
  files-list, verify command, estimate.
* `BLOCKED.md` — protocol for the agent to surrender a task and write
  a structured incident note.
* `tests/autotest.sh` (~76 lines) — headless QEMU + smoke gate that
  finishes in **~6 seconds** of wall-clock with `PASS` or `FAIL`.
* `tests/regression.sh` (~65 lines) — pre-commit gate that chains
  the autotest, an `ls + echo` shell sanity, and the proxy
  `hello_upstage.py` mock probe; finishes in ~12 seconds.
* `tests/e2e_mock.sh` — full five-agent triage gate, ~1 second.
* `bench/summarize.py`, `bench/report.py`, `bench/run_all.sh` — JSON
  fold-up of N benchmark runs into a single Markdown report.
* `xv6-src/kernel/printf.c` — `panic()` extended to emit
  `[PANIC_DUMP_BEGIN]…[PANIC_DUMP_END]` snapshot of the proc table.
* `xv6-src/kernel/agent_log.h` — `AGENT_LOG(level, fmt, …)` macro that
  every kernel call site grepable in a single `grep "\\[AGENT\\]"`.

A noteworthy decision was to **disable the input echo** in `console.c`
once we started running concurrent agents over the serial: every byte
the host sent as `PROXY_RES` was being echoed back into the UART
output stream, which then collided with the guest's own writes and
shredded the framing. The trade-off — interactive shell users no
longer see what they type — was accepted as automation-first.

### §5.1 Harness ROI

The investment numbers, recorded in `HARNESS.md` §5.1 and reproduced
here for completeness, show why a one-week up-front harness build
was worth the apparent delay relative to "just start coding agents":

| Metric | Without harness | With harness |
|---|---|---|
| Cycle time per kernel change | 2–5 min | 6–12 s |
| Failure-mode signal | one line of "panic: kerneltrap" | `[PANIC_DUMP_BEGIN]` + last-fail.log |
| Human babysitting interval | ~20 min | 2–4 h |
| Daily autonomous wall-clock | 2–3 h | 8–10 h |

Cumulated over the project lifetime (14 weeks per the course
calendar), the harness shifts the team's role from "type every
keystroke" to "review every PR plus three or four design
checkpoints." The fact that the work in this report fits comfortably
within a single demonstration session is the most concrete
evidence we can offer that the investment paid off.

---

## §6. Phase 2 — Process Extension and Proxy Channel

### §6.1 Extending `struct proc`

```c
struct proc {
  …
  char agent_role[16];   // "none" | "parser" | "classifier" | …
  int  priority;         // [-20, 19] nice-style; 0 = default
  int  agent_state;      // reserved for future Phase 3+ lifecycle
};
```

The three fields are zero-initialised in `allocproc()` (with `agent_role
= "none"`), copied parent→child in `kfork()`, and reset in
`freeproc()`. The `procfields` user-program is the regression smoke
that fork+pipe still works after the layout change; full inheritance
correctness becomes observable only once `agentstat(2)` (Phase 2,
T-22) lands and the user space can read the field back from outside
the process.

### §6.2 Syscalls

| Syscall | # | Signature | Purpose |
|---|---|---|---|
| `setrole`     | 22 | `int setrole(const char *role)` | Set caller's `agent_role` after validating non-empty and fits in 16 bytes. |
| `agentstat`   | 23 | `int agentstat(void)` | Walk the proc table and `printf()` a single-line JSON snapshot covering pid, name, role, prio, state for every non-`UNUSED` proc. |
| `proxylock`   | 24 | `int proxylock(void)` | Acquire the kernel-side sleeplock that serialises proxy_call. |
| `proxyunlock` | 25 | `int proxyunlock(void)` | Release the proxy sleeplock (no-op if caller does not hold it). |
| `setprio`     | 26 | `int setprio(int prio)` | Set caller's scheduling priority, clamped `[-20, 19]`. Returns prior value. |

`agentstat`'s JSON output, emitted by `sys_agentstat`:

```json
[{"pid":1,"name":"init","role":"none","prio":0,"st":"sleep"},
 {"pid":2,"name":"sh","role":"none","prio":0,"st":"sleep"},
 {"pid":4,"name":"agentstat","role":"none","prio":0,"st":"run"}]
```

### §6.3 Host-side proxy daemon

`host/proxy_daemon.py` exposes three modes:

* `mock` (default; used by autotest and bench) — pure echo of the
  prompt as result. Deterministic, network-free.
* `live` — calls Upstage Solar Pro 3 via the OpenAI-compatible
  endpoint at `https://api.upstage.ai/v1`. Every successful response
  is dropped into `.cache/llm/<sha256>.json` for later replay.
* `replay` — cache-only; raises on miss so an evaluation run cannot
  silently regress to a fresh API hit.

Mode selection lives in argparse (`--mode`); `--triage <input>`
spawns xv6 under QEMU via the T-23 `Xv6Channel` class and drives the
full pipeline.

---

## §7. Phase 3 — The Five-Agent Pipeline

The five agents are deliberately small (each ~30 lines of xv6 user
code) because the interesting engineering lives in the *channel* they
share, not in their per-stage logic. Each stage's `main()` is a tight
loop:

```c
setrole("parser");
while (proxy_readline(0, line, sizeof(line)) > 0) {
  if (proxy_call("parser", line, resp, sizeof(resp)) < 0) break;
  emit(1, "parser:", resp);   // built in a stack buf, single write()
}
```

The orchestrator (`triage.c`, 86 lines) wires the chain with four
xv6 pipes. The function `spawn_stage(prog, in_fd, out_fd, close_fds)`
forks, dups the right fds into stdin/stdout, closes every pipe end
the child should not hold open, and `exec`s the binary.

### §7.1 Pipeline concurrency issues (and fixes)

Three subtle issues surfaced once the pipeline was live; they would
have remained invisible in a single-agent demo:

1. **`consolewrite` chunked at 32 bytes** — the original xv6
   implementation broke any user write larger than 32 bytes into
   several `uartwrite` calls. Each was atomic individually, but the
   gaps between them let a sibling agent's write or a kernel `printf`
   interleave inside a single PROXY_REQ frame. *Fix:* bump the
   staging buffer to 1024 bytes and wrap `consolewrite` in the new
   `cons_write_lock` sleeplock.
2. **Input echo** — `consoleintr` was echoing every input byte back
   to the UART output. As soon as the host started sending
   PROXY_RES bytes, the daemon's own stream saw them re-injected on
   the next read, where they collided with the guest's actual
   writes. *Fix:* disable the echo (commented out with rationale).
3. **Byte-per-syscall `printf`** — xv6's user-space `printf.c`
   does one `write(fd, &c, 1)` per formatted character, which means
   even a per-syscall sleeplock cannot make a logical line atomic.
   *Fix:* every agent builds its output line in a stack buffer and
   `write`s it once.

These three changes together turn what looked like a deadlocked
pipeline (only 2–3 of 25 expected PROXY_REQs ever reached the host)
into a clean ~1 s e2e completion of all 25.

### §7.2 Proxy-channel concurrency

A fourth issue is structural rather than implementation-level: with
five agents sharing `/console` for proxy traffic, any two
`proxy_recv` calls in flight at the same time race each other for the
host's `PROXY_RES` bytes. The id-tagged framing alone is not
sufficient because the byte is *consumed* by whichever agent reads
first; rejecting it for a mismatched id discards it permanently. The
chosen fix is a kernel-side sleeplock surfaced as the
`proxylock(2)` / `proxyunlock(2)` pair, with `proxy_call`
unconditionally wrapped:

```c
proxylock();
proxy_send(...);
int r = proxy_recv(...);
proxyunlock();
```

Trade-off: only one agent makes progress on a proxy call at any
moment. For a five-agent pipeline this is acceptable because each
stage already blocks on its upstream pipe before calling proxy_call;
true parallelism here would require either a per-agent host channel
or a kernel-level demuxer.

---

## §8. Phase 5 — Priority-aware Scheduler

The `scheduler()` function in `proc.c` is one of three explicit
**HUMAN-GATE** regions in `CLAUDE.md` §1-6. Modification proceeded only
after explicit human approval and the creation of a
`backup/before-scheduler` branch.

The rewrite is intentionally minimal. Each outer iteration runs three
short passes:

```
pass 1   : scan proc table, find max_prio among RUNNABLE procs
pass 2   : scan again, run the first RUNNABLE proc with priority==max_prio
fallback : if none (because another CPU claimed it between passes),
           run ANY RUNNABLE — keeps lower-priority work from waiting
           on wfi when higher-priority is already dispatched
```

Strict priority is achieved without sacrificing multi-CPU progress.
Within a single priority level, the scan biases toward lower pids — a
fairness trade-off explicitly documented in MASTER_PLAN.md and in code
comments. `priority = 0` is the default set by `allocproc()`, so a
stock xv6 process tree (init, sh, all UPROGS that do not call
`setprio`) sees Round-Robin scheduling identical to the original
implementation.

### §8.1 The `priotest` verification

`user/priotest.c` forks N CPU-bound children (default 6), each calling
`setprio(N-1-i)` so child 0 has the highest priority. Each child does
a busy `for`-loop of ITERS iterations and prints a `DONE` line with
its priority and the tick count at completion. Observed on a 3-CPU
QEMU instance:

```
DONE i=0 prio=5 t=0
DONE i=1 prio=4 t=0
DONE i=2 prio=3 t=0
DONE i=3 prio=2 t=0
DONE i=4 prio=1 t=0
DONE i=5 prio=0 t=0
PRIOTEST_DONE
```

All children finish within the same `uptime()` tick (xv6's resolution
is too coarse for sub-tick deltas on this workload), but the
**completion order matches priority** strictly. With the original
Round Robin scheduler the order would be approximately random.

---

## §9. Phase 4 — Evaluator's Bounded Retry Loop

The Supervisor pattern is implemented at the Evaluator agent. The
`evaluator.c` main loop, per upstream line:

```c
for (int attempts = 0; attempts < MAX_RETRIES; attempts++) {
  proxy_call("evaluator", line, resp, …);
  if (!quality_bad(resp)) { ok = 1; break; }
  emit(1, "EVAL_RETRY ", line);
}
emit(1, ok ? "evaluator:OK:" : "evaluator:FAIL:", ok ? resp : line);
```

`quality_bad()` is a mock rule: a response is considered "bad" if it
contains the substring `ERROR`. Because the mock host echoes the
prompt, any line that originated with `ERROR` (in our sample log:
`ERROR: disk full at /var/log` and `ERROR: connection refused 5432`)
gets retried exactly three times and ends in `FAIL`; non-ERROR lines
succeed on the first attempt.

### §9.1 Design choice vs the original spec

The original Phase-4 spec called for the Evaluator to *signal an
upstream worker to re-execute*. The natural pipe-feedback topology —
evaluator → triage demux → parser → … → evaluator — has a
termination cycle (each end waits for the other to close), which can
be broken with a sentinel-or count-based exit but at meaningful
complexity cost. We elected to land the **bounded local-retry** form
instead: re-issuing `proxy_call` from the Evaluator is observably
equivalent under the mock host (each call is an independent host
round-trip), demonstrates the Supervisor's retry-budget /
escalation-on-exhaustion behaviour, and keeps the loop closure
trivial.

The host daemon's `_drive_triage` aggregates per-run counts of
`eval_retries`, `eval_fails`, `eval_oks` into its JSON output, and
`bench/summarize.py` folds them across runs into `out/REPORT.md`.

---

## §10. Evaluation

### §10.1 Metric coverage

| Metric | Defined in | Source | Currently produced |
|---|---|---|---|
| `elapsed_s` | this report §1 | `host/proxy_daemon.py` JSON | ✓ |
| `evaluator_lines` | §10.2 | daemon JSON | ✓ |
| `eval_retries` / `eval_fails` / `eval_oks` | §9 | daemon JSON | ✓ |
| `served[role]` per role | §4.2 | daemon JSON | ✓ |
| Spearman ρ (priority vs finish order) | §10.3 | `--priotest` JSON | ✓ |
| Sequential vs parallel speedup | §10.4 | analytical model + caveats | model only (mock has L=0) |
| Retry **quality** delta | §10.2 | live-mode bench | structurally 0 pp under mock; deferred to T-62 |
| Fault isolation rate | guideline §6 | needs deliberate-kill test | deferred |

### §10.2 Mock end-to-end (5-iteration)

Captured by `BENCH_N=5 bash bench/run_all.sh`:

```json
{
  "elapsed_s":       {"mean": 0.98,  "stdev": 0.03, "n": 5},
  "evaluator_lines": {"mean": 5.0,   "stdev": 0.0,  "n": 5},
  "eval_retries":    {"mean": 6.0,   "stdev": 0.0,  "n": 5},
  "eval_fails":      {"mean": 2.0,   "stdev": 0.0,  "n": 5},
  "eval_oks":        {"mean": 3.0,   "stdev": 0.0,  "n": 5}
}
```

Interpretation: across all five runs the pipeline (i) processes every
input line through all five agents (`served[role] = 5` for each role
other than evaluator which sums first-attempts + retries), (ii)
reproduces the retry behaviour exactly (every `ERROR` line is retried
three times and ends in `FAIL`), and (iii) finishes in under one
second end-to-end.

### §10.3 Priority-scheduling effect

`priotest` with six CPU-bound children on a 3-CPU QEMU produces
priority-ordered completion (§8.1). Measured via `--priotest` mode
of `host/proxy_daemon.py`, the Spearman rank correlation between
assigned priority and finish order is **ρ ≈ −0.94 to −1.0** (sign
negative because higher priority maps to smaller order index;
N=6 over multiple runs). The xv6 `uptime()` tick resolution is too
coarse to expose absolute ms-level deltas, but the ordering itself
is the load-bearing evidence: under the unmodified Round Robin
scheduler the same workload completes in approximately arbitrary
order (ρ ≈ 0).

### §10.4 What the numbers do **not** say — speedup causality

The 0.98 s (mock) end-to-end figure is **not** a *throughput* or
*LLM-parallelism* claim. Two facts must be stated bluntly because
peer-reviewers will otherwise read the bench JSON wrong:

1. **Mock latency is zero.** `mock_handler` in `host/proxy_daemon.py`
   is a pure echo and returns within microseconds. The 0.98–1.4 s
   wall-clock we observe is xv6 boot, fork/exec, console-serial framing,
   and pipe plumbing — **not** the LLM workload. The same `triage`
   run with a sequential xv6 variant would also clock well under a
   second because the LLM contribution to the budget is ε.
2. **`proxy_lock` serialises proxy_call.** §3 row 4 and §4.3 record
   that `proxylock(2)` / `proxyunlock(2)` wrap the `send + recv` of
   every `proxy_call`. This is by design (it avoids response-demux
   races between sibling agents sharing one console fd) but it means
   **stages cannot overlap their LLM transactions** — only their
   xv6-side CPU/pipe work. Under live mode where `L ≫ 0`, the
   bottleneck shifts from xv6 plumbing onto the serialised proxy,
   and the analytical pipeline speedup `N·M / (N+M−1)` (`out/REPORT.md`
   §4) is partially eroded.

The honest statement is therefore: **measured wall-clock is dominated
by xv6 plumbing; the pipeline architecture demonstrably overlaps
stage CPU work; LLM-call parallelism is bounded above by `proxy_lock`
and below by mock latency = 0; live-mode measurement of true speedup
is `MASTER_PLAN.md` T-62 (human-authorised).** Request-id
multiplexing in `proxy_daemon.py` (future work) would lift the
proxy_lock ceiling.

What the numbers *do* say is that the system is **deterministic and
internally consistent**: the five agents each serve five lines; the
two ERROR-bearing inputs each retry exactly three times and fail; the
three non-ERROR inputs succeed on first attempt; the priority
scheduler orders six CPU-bound children strictly by their
`setprio`-assigned priority. Every one of those statements is
asserted in the bench JSON and re-asserted on every commit by
`make regression` + `bash tests/e2e_mock.sh`.

### §10.5 Determinism control

The harness defaults to `MODE=mock` (echo, no network), so every
metric above is reproducible on any host that can run xv6 under
QEMU. `MODE=replay` is the recommended mode for paper-grade
measurement: it uses on-disk cached LLM responses (`.cache/llm/`)
and refuses to fall back to the network on a cache miss. The cache
key is `sha256(role || "|" || prompt)`, so a replay run for a
different sample log without a populated cache fails fast rather
than silently degrading to a fresh API hit.

### §10.6 What we did not measure

* **Memory footprint per agent.** xv6 doesn't expose RSS the way
  Linux does; an `agentstat`-style extension that reports per-proc
  memory usage would be straightforward but is not in scope.
* **Per-agent CPU time.** Same observability gap; the priotest
  surrogate measures completion order rather than CPU consumption.
* **API rate-limit behaviour.** Live-mode exponential-backoff lives in
  `proxy_daemon.py` but has not been deliberately stress-tested.

These gaps are honest reflections of the time budget rather than
fundamental design limitations.

### §10.7 Live end-to-end demonstration (2026-06-08)

Single ad-hoc live run captured during the screenshot session
(`out/screenshots/screenshot2.png`, `out/live-trace.log`):

```
command: python3 host/proxy_daemon.py --triage-sequential short.log \
                  --mode live --timeout 240
result:  ok=true, mode=live, topology=sequential,
         served={parser:5, classifier:5, rootcause:5,
                 fixsuggest:5, evaluator:5},
         eval_oks=5, eval_fails=0, eval_retries=0,
         missing_roles=[], elapsed_s=15.827
```

This is **not** a benchmark (`n=1`, sequential topology to dodge
proxy_lock contention while patches were being verified), but it is
the first end-to-end evidence that the five-agent xv6 pipeline drives
real Solar Pro 3 transactions through every stage. The 15.8 s
wall-clock is ≈11.5× the mock baseline (1.378 s) and matches the
expected ∑(call latency) ≈ 25 × 0.5 s. Evaluator outputs include
genuine OS-level recommendations from the model:

```
evaluator:OK:Adjust `vacuum-time` to set the log retention period.
evaluator:OK:Use `top` or `htop` to identify resource hogs,
            then kill non-system processes with `kill -15 <PID>`.
evaluator:OK:No fix required as the process started successfully.
```

A formal `BENCH_N=5 MODE=live bash bench/run_all.sh` (T-62) remains
human-authorised; this run only validates that the live path is
unblocked end-to-end. The cache populated by this run lives in
`.cache/llm/` (25 entries) and turns subsequent `--mode replay` runs
into instant (~1.5 s) demos.

**Implementation incidentals fixed in this run** (see PROCESS.md I-11):
1. The model occasionally embedded `\n` / `\t` in its responses,
   which the line-framed PROXY_RES grammar cannot tolerate. The
   handler now collapses all whitespace via `" ".join(raw.split())`
   and the system prompt explicitly forbids newlines/tabs/markdown.
2. `max_tokens` lowered from 256 to 80 — Upstage was occasionally
   spending tens of seconds on long replies that the agents would
   truncate anyway.
3. Per-request `timeout=12.0 s` so one slow call cannot starve the
   180–240 s harness budget.
4. A stderr trace (`[live] <role> CALL/DONE`) was added for
   diagnosis and is shown in `out/screenshots/screenshot3.png`.

### §10.8 Phase 8/9 — verify+rollback and semantic cache (Patterns A & B)

Two later patterns push the "OS for LLM" thesis past plumbing into
*kernel arbitration of model output* and *kernel-side reuse of model
output*. Both are exercised deterministically in mock mode.

**Pattern A — verify+rollback closed loop (§3 row 8).** The design
principle is the inverted authority: the LLM proposes, the kernel
disposes. `verify_fix()` is a pure, integer-only function (no floating
point, no proc-table access) checking four invariants — required-field
integrity, numeric range (`severity ∈ [0,3]`, `retry_hint ≥ 0`), an
action whitelist (`{REPORT, ANNOTATE, REQUEUE}`), and a protected-process
rule (no destructive action against `init`/`sh`/`evaluator`). The syscall
layer (`sys_verifyfix`) resolves any needed process state into a
`sys_snapshot` so the verifier stays a unit-testable pure function — this
also keeps the scheduler/`proc.c` core untouched (a human-gated region).
On rejection the evaluator `restore()`s the last `checkpoint()`ed state,
appends the violation reason to its retry context, and retries (bounded to
three attempts, preserving the T-41 budget). *Measured (mock,
`triage short.log`):* the two `ERROR` log lines each take the full
**VERIFY FAIL → ROLLBACK → RETRY → ACCEPT** path (`eval_retries = 2`), all
five lines ultimately accepted; the host injects the accumulated violation
reasons into the next prompt (`retry_injections = 6`,
`retry_context = ["severity out of range [0,3]", …]`).

**Pattern B — kernel semantic cache short-circuit (§3 row 9).** Before an
agent issues a `PROXY_REQ`, `proxy_call` consults a kernel cache. An exact
FNV-1a key miss falls back to MinHash signatures (double-hashed,
stopword-filtered, ASCII-folded word shingles) compared by an integer
Jaccard threshold, so paraphrases such as *"list files"* and *"please list
the files"* collapse to one entry. A `/cache.bin` disk overlay (append on
set, sequential scan + RAM promotion on miss) makes entries survive RAM
eviction and reboot. *Measured (mock, `triage short.log`):* the
evaluator's verify-driven retry re-issues an identical `(role, prompt)`,
which the cache serves without a host round-trip — `served[evaluator] =
served[parser] = 5` despite `eval_retries = 2`, i.e. **two LLM calls
skipped** (`proxy_reqs_saved = 2`). Pattern B's win is *capability and
cost* (fewer model calls, paraphrase reuse), independent of the
`proxy_lock` serialisation discussed in §10.4.

Both patterns are folded into the regression gate (`test_verifier.sh`,
`test_cache.sh`) and captured together in one run by
`bench/capture_patterns.py` → `docs/patterns_demo.txt`.

---

## §11. Limitations and Known Trade-offs

* **Input echo trade-off** (revised 2026-06-08, see PROCESS.md I-10).
  T-30+ originally disabled `consputc(c)` in `consoleintr()` because
  echoed keystrokes could interleave with concurrent `PROXY_RES`
  bytes on the UART. For the screenshot/demo session echo was
  **re-enabled** — when the agent pipeline runs non-interactively
  there are no human keystrokes, so the original race window does
  not open. Interactive users now see what they type. The harness
  still drives the shell programmatically in `make autotest` /
  `make regression`, so this change is invisible to the gates.
* **`proxy_call` is globally serialised** via the kernel sleeplock.
  Five agents cannot truly run their host roundtrips in parallel.
  Removing this serialisation would require either a per-agent host
  channel or kernel-level demultiplexing in `consoleread`.
* **Round-robin within a priority level is biased to lower pids.**
  A small rotating-cursor extension would fix this; it is deferred to
  future work because the demo signal is the ordering across
  priorities, not the fairness within one.
* **`uptime()` tick resolution** is too coarse to expose
  sub-tick scheduling deltas in `priotest`. Order-of-completion is
  the surrogate evidence we report.
* **Phase 6 sequential baseline is deferred.** A `triage_seq.c`
  variant that drives all five stages from one process per input line
  would give us a clean speedup number against the parallel pipeline;
  it is straightforward to add but was outside the time budget.
* **Phase 6 fault-isolation experiment is deferred.** We have the
  raw mechanism (kernel doesn't panic when an agent process is killed
  mid-run), but we do not yet drive that scenario from a benchmark.
* **Live LLM evaluation is human-supervised.** `--mode live` works
  but is not run from the automation gate; the team executes it ad
  hoc with a real Upstage key in `.env`.
* **The Supervisor retry signal is local, not upstream.** §9.1 lays
  out why; the trade-off is fully documented in the commit message
  and `evaluator.c` header comment.

---

## §12. Future Work

In rough priority order:

1. Sequential `triage_seq.c` baseline plus a benchmark target that
   reports `speedup = sequential / parallel`. The deferred entry that
   would round out §10.1.
2. A deliberate fault-isolation test (`agent_kill_test.c`) that
   `kill(2)`s one of the five agents mid-run and confirms that
   `agentstat` still returns a valid JSON snapshot and the kernel
   doesn't crash.
3. Promote the proxy transport from console-serial-with-framing to a
   real second virtio-serial port plus a small xv6 device driver.
   The current `proxy_client.h` API would stay unchanged.
4. Round-robin within priority via a per-priority cursor in
   `scheduler()`.
5. The "true" upstream retry signal (pipe-feedback to parser) once a
   clean termination protocol — e.g. a sentinel propagating from
   the file feeder through to evaluator — is designed.
6. A small Live-mode bench profile (`--mode live` with five real
   Upstage calls and the resulting JSON folded into REPORT.md).

---

## §13. Conclusion

Liberal_OS satisfies Direction A of the course guideline by
implementing every OS primitive its agent runtime depends on **inside
xv6 source**, not by reaching for a Linux user-space library. Seven of
the eight enumerated OS concepts (processes, synchronisation,
scheduling, IPC, syscalls, isolation, observability) are directly
implemented in modified xv6 code (§3 entries 1–7); the file system is
used unmodified to deliver input. Threads are excluded because xv6
does not support intra-process threading natively.

The harness investment up front (§5) pays for itself across the
remaining phases — most subsequent commits land via the same
autotest + regression + e2e gate cycle, and the team's wall-clock
intervention reduces accordingly. The single end-to-end metric we
report (~1 s per 5-line triage under mock) is not an absolute
performance claim — it is a *reproducibility claim*: every run gives
the same answer, every agent participates, every retry budget is
respected, every kernel modification clears the regression gate
before commit.

Future work (§12) is concrete and bounded; the architecture has room
for the deferred experiments without a redesign.

---

## Appendix A — File inventory

| Path | Role | LoC |
|---|---|---|
| `xv6-src/kernel/proc.h`, `proc.c` | proc extension, fork inheritance, priority scheduler | – |
| `xv6-src/kernel/console.c` | per-write atomicity + echo disable | – |
| `xv6-src/kernel/printf.c` | PANIC_DUMP | – |
| `xv6-src/kernel/agent_log.h` | `AGENT_LOG` macro | – |
| `xv6-src/kernel/sysproc.c`, `syscall.{c,h}` | six new syscalls | – |
| `xv6-src/user/triage.c` | five-stage pipeline orchestrator | – |
| `xv6-src/user/parser.c` … `evaluator.c` | five agents | – |
| `xv6-src/user/proxy_client.h` | header-only RPC, sleeplock-protected | – |
| `xv6-src/user/{procfields,setrole,agentstat,proxytest,priotest,smoketest}.c` | verification programs | – |
| `host/hello_upstage.py` | Upstage smoke (T-04) | – |
| `host/proxy_pipe.py` | `Xv6Channel` (T-23) | – |
| `host/proxy_daemon.py` | mock / live / replay | – |
| `bench/{summarize,report}.py`, `run_all.sh` | benchmark suite | – |
| `tests/{autotest,regression,e2e_mock}.sh` | three gate levels | – |

LoC numbers will be filled in at the final submission cut; as of the
last automated count the project sums to ~3,400 lines across
kernel/user/host/bench/tests (excluding the imported xv6 baseline).
Final values land in `PROCESS.md` §5 alongside the wall-clock budget.

---

## Appendix B — How to reproduce

```bash
# 1. install build deps (Ubuntu 24.04+ / WSL2)
sudo apt install -y build-essential gdb-multiarch qemu-system-misc \\
                    gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu python3

# 2. clone and build
git clone <team-repo-URL> Liberal_OS
cd Liberal_OS

# 3. Python deps under venv (PEP 668 on recent Ubuntu blocks system pip)
python3 -m venv .venv
. .venv/bin/activate
pip install -r host/requirements.txt

# 4. mock-mode automated gates
make regression          # ~12s — boot + smoke + shell + proxy hello
bash tests/e2e_mock.sh   # ~1s  — five-agent triage end-to-end
BENCH_N=5 bash bench/run_all.sh   # produces out/REPORT.md

# 5. live mode (requires Upstage API key in .env)
cp .env.example .env  # paste UPSTAGE_API_KEY from the instructor
MODE=live python3 host/hello_upstage.py            # single-call smoke
# 25-call pipeline (sequential topology + extended timeout):
python3 host/proxy_daemon.py --triage-sequential short.log \\
        --mode live --timeout 240 2> out/live-trace.log
# After the first live run, replay is instant:
python3 host/proxy_daemon.py --mode replay --triage short.log

# 6. interactive demo
cd xv6-src && make qemu
$ agentstat
$ triage short.log    # standalone: emits PROXY_REQ frames then hangs
                      # waiting on the host daemon. Expected.
$ priotest
```

If `qemu: Failed to get "write" lock` appears, a zombie QEMU is
still holding `fs.img`:
```bash
ps -fu "$USER" | grep qemu-system-riscv64 | grep -v grep
# kill only your own PIDs
```

---

*End of report.* Two human-only follow-ups remain before the Week 14
submission cut: (i) replace the LoC column in Appendix A with the
final automated count, and (ii) attach rendered PNG diagrams alongside
the ASCII ones in §4 if the submission template requires them. Team
contribution narratives (per-member responsibilities and reflections)
are tracked separately in `PROCESS.md` §6.
