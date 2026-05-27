# Liberal_OS — Presentation Slide Draft (English-only)

> 15-minute final presentation. One H2 per slide, speaker notes in italics
> right under the title. Source for `slides/final.pptx`.
> Reproducibility links: `MASTER_PLAN.md`, `docs/TECHNICAL_REPORT.md`.

---

## Slide 1 — Title

**Liberal_OS: an xv6 kernel that directly manages LLM agent processes**

Team Liberal_OS — 2026 Spring OS Course
Direction A (OS for LLM)

*Speaker (≈30s): introduce team, name the direction, hand off to motivation.*

---

## Slide 2 — Motivation in one line

> *"Not 'call the LLM API with multiprocessing' — but 'xv6 directly manages the LLM processes.'"*

Why we cannot use the easy path:

- Guideline §2 disqualifies "thin wrappers around an LLM API."
- Guideline §2 disqualifies OS aspects amounting to "it runs on Linux."
- So every OS primitive our agent runtime needs **must live inside xv6 source.**

*Speaker (≈45s): hammer the slogan, quote guideline §2 once, set the stakes.*

---

## Slide 3 — Killer scenario: 5-stage log triage pipeline

```
[raw log] → parser → classifier → root-cause → fix-suggester → evaluator → [report]
                                                                  │
                                                          retry signal (≤3)
```

Five xv6 user processes, four pipes, one host-side Upstage Solar Pro 3 transport.

Why this scenario *needs* OS decisions:

- High volume → parallel processes
- One bad line must not kill the pipeline → process isolation
- Quality-gated retries → Supervisor pattern → IPC + signals
- "Critical now, info later" → priority scheduling

> **Framing note.** The workload (log triage) is borrowed from
> GuideLine §2's Direction-B examples, but our deliverable is the
> **A-side OS mechanisms** that run it. We picked a B-flavored
> workload because it stresses isolation, IPC, scheduling, and
> syscalls harder than a generic chat agent would.

*Speaker (≈60s): walk through the diagram left-to-right. Land on the four reasons.
If asked "isn't this Direction B?" — point to the framing note: the WORKLOAD is
B-flavored on purpose; the CONTRIBUTION is the A-side xv6 mechanisms in §3.*

---

## Slide 4 — OS concept mapping (the table we keep returning to)

| # | Liberal_OS component | OS concept | xv6 file(s) we modified |
|---|---|---|---|
| 1 | Per-agent metadata | Process management | `proc.h`, `proc.c` |
| 2 | Pipeline backbone | IPC (pipes) | `triage.c` (uses `pipe.c`) |
| 3 | Host transport | IPC (line-framed) | `proxy_client.h`, host `proxy_daemon.py` |
| 4 | Mutual exclusion | Synchronisation (sleeplocks) | `console.c`, two new syscalls |
| 5 | Agent priority | Scheduling | `proc.c::scheduler()` + `setprio(2)` |
| 6 | Fault containment | Process isolation | inherent in xv6 |
| 7 | Observability | System calls | six new syscalls incl. `agentstat(2)` |

Seven of the eight enumerated OS concepts are exercised directly in xv6 source.

*Speaker (≈60s): point to row 5 (scheduler) and row 4 (sleeplocks) as the two most substantive edits.*

---

## Slide 5 — Architecture: host vs. guest

```
┌──────────────────────────────────────────────┐
│              QEMU host (Linux)               │
│                                              │
│  ┌────────────────────────┐  ┌─────────────┐ │
│  │   xv6 guest            │  │ Proxy       │ │
│  │ ┌────────────────────┐ │  │ Daemon      │ │
│  │ │ orchestrator       │ │  │ (Python)    │ │
│  │ │ fork → 5 agents    │◄┼──┤ Upstage API │ │
│  │ │ pipes wire stages  │ │  │ live/replay │ │
│  │ │ sleeplock serial.  │ │  │ /mock       │ │
│  │ └────────────────────┘ │  └─────────────┘ │
│  └────────────────────────┘                  │
└──────────────────────────────────────────────┘
```

xv6 has no network stack — the proxy daemon owns the API call.
From the guest's point of view, an LLM call is a `read`/`write` pair on a console fd.

*Speaker (≈45s): emphasize that the abstraction boundary is the *guest* — xv6 never knows an LLM exists.*

---

## Slide 6 — Proxy transport: from virtio dream to console-serial reality

What we wanted:

- A dedicated virtio-serial port to the host proxy.

What we shipped:

- Line-framed protocol layered on the existing console UART:
  - request: `PROXY_REQ\t<id>\t<role>\t<prompt>`
  - response: `PROXY_RES\t<id>\t<result>`
- pid as the demultiplex id (5 siblings, one shared fd).
- Sleeplock + `proxylock(2)`/`proxyunlock(2)` syscalls so one agent's
  send+recv is atomic against a sibling's.

Trade-off: interactive echo is disabled. Acceptable — the harness drives shell programmatically.

*Speaker (≈60s): "every shortcut had a documented cost."*

---

## Slide 7 — Supervisor pattern: bounded-retry evaluator

```
worker ──result──► evaluator
  ▲                  │
  └── retry (≤3) ────┘   then "evaluator:FAIL" if budget exhausted
```

- Evaluator decides PASS/FAIL with a quality rule.
- On FAIL, signals the worker to retry — bounded at 3 attempts (T-41).
- After 3 failures the line is tagged `evaluator:FAIL` and the pipeline moves on.

Verified by injecting a mock response that always fails: exactly 3 retries, then `FAIL`.

*Speaker (≈45s): name-check the bound (3) and why infinite retries are unsafe (I-05).*

---

## Slide 8 — Priority scheduling: opt-in, regression-safe

- New field `priority` in `struct proc` (default 0).
- `scheduler()` does max-priority selection among `RUNNABLE` procs.
- `setprio(int)` syscall, nice-style range `[-20, 19]`, clamped.
- **Default behaviour collapses to baseline Round Robin** when no one sets priority.

Demo evidence: `priotest` launches two children at different priorities;
high-priority child always completes first.

*Speaker (≈45s): emphasise the safety property — no priority set, no behaviour change.*

---

## Slide 9 — Harness: how four humans drove this on a tight schedule

- `make autotest` — ~12s headless xv6 boot + smoke.
- `make regression` — autotest + shell sanity + proxy hello (~12s, mandatory pre-commit).
- `bash bench/run_all.sh` — 5x mock-mode triage → `out/REPORT.md`.
- `MASTER_PLAN.md` Part II is the T-NN task queue.
- 🔴 HUMAN GATE marks every irreversible change (scheduler, force-push).

Why this matters: it let Claude Code progress safely between sync meetings.

*Speaker (≈60s): point at the regression gate — "this is the line that made every commit safe."*

---

## Slide 10 — Evaluation: what we measured

Conditions: `--mode replay` (cached LLM responses, no API variance), 5 iterations,
5-line input log, 5 agents.

| Metric | Mean | Stdev | N |
|---|---:|---:|---:|
| End-to-end elapsed (s) | 1.22 | 0.22 | 5 |
| Evaluator OKs / run | 3.0 | 0.0 | 3 |
| Evaluator FAILs / run | 2.0 | 0.0 | 3 |
| Evaluator retries / run | 6.0 | 0.0 | 3 |
| Evaluator output lines | 5.0 | 0.0 | 5 |

Source: `out/REPORT.md` (auto-generated by `bench/report.py`).

*Speaker (≈45s): point at the zero stdev rows — "this is reproducibility, not raw speed."*

---

## Slide 11 — Honest limitations

- `proxy_call` is globally serialised (one sleeplock) — true host-side parallelism is future work.
- Sequential baseline (`triage_seq.c`) not yet shipped → no clean speedup number.
- Fault-isolation test exists as raw mechanism but not yet a benchmark.
- `uptime()` tick resolution too coarse — we report order-of-completion, not microseconds.
- Live mode is human-supervised (T-62), not part of the gate.
- Evaluator retry signal is local (re-issue self) — not yet propagated upstream.

Every limitation is written down in `docs/TECHNICAL_REPORT.md` §11.

*Speaker (≈60s): "we list what we did *not* do — none of it is a surprise."*

---

## Slide 12 — Future work (concrete, bounded)

1. `triage_seq.c` sequential baseline + speedup report.
2. `agent_kill_test.c` for explicit fault-isolation benchmark.
3. Real virtio-serial port + xv6 device driver (replace console-framing).
4. Within-priority round-robin cursor in `scheduler()`.
5. Upstream retry signal once a termination protocol is designed.
6. Live-mode bench profile folded into `REPORT.md`.

None of these require a redesign.

*Speaker (≈30s): "Architecture has room — these are deliverables, not pivots."*

---

## Slide 13 — Reproducibility

```bash
# 1. install build deps (Ubuntu 24.04+)
sudo apt install -y build-essential gdb-multiarch qemu-system-misc \
                    gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu python3

# 2. clone and build
git clone <team-repo-URL> Liberal_OS && cd Liberal_OS

# 3. gates
make regression
bash tests/e2e_mock.sh
BENCH_N=5 bash bench/run_all.sh

# 4. live demo (needs UPSTAGE_API_KEY in .env)
cd xv6-src && make qemu
$ triage short.log
$ agentstat
$ priotest
```

*Speaker (≈30s): everything is reproducible from a clean Ubuntu 24.04 machine.*

---

## Slide 14 — Q&A

**Anticipated questions:**

- *"Why not just use Python multiprocessing?"* → Slide 2 + Slide 4 (rows 1–7).
- *"Did you really modify the scheduler?"* → `proc.c::scheduler()` diff + Slide 8.
- *"How do you know retries are bounded?"* → Slide 7, mock test.
- *"What is the proxy protocol exactly?"* → Slide 6.
- *"What about live-mode performance?"* → Slide 11 limitations.

*Speaker (≈30s): invite questions. If asked about LoC, the Technical Report Appendix A has it.*

---

## Slide 15 — Thank you

**Liberal_OS** — xv6 + Upstage Solar Pro 3 + four people + one harness.

Repository: `<team-repo-URL>`
Technical report: `docs/TECHNICAL_REPORT.md`
Process document: `PROCESS.md`

*Speaker (≈10s): thanks, name the team again, stop.*

---

## Timing budget (target: 15 minutes)

| Slide | Topic | Target seconds | Cumulative |
|---|---|---:|---:|
| 1 | Title | 30 | 0:30 |
| 2 | Motivation | 45 | 1:15 |
| 3 | Killer scenario | 60 | 2:15 |
| 4 | OS concept mapping | 60 | 3:15 |
| 5 | Architecture | 45 | 4:00 |
| 6 | Proxy transport | 60 | 5:00 |
| 7 | Supervisor pattern | 45 | 5:45 |
| 8 | Priority scheduling | 45 | 6:30 |
| 9 | Harness | 60 | 7:30 |
| 10 | Evaluation | 45 | 8:15 |
| 11 | Limitations | 60 | 9:15 |
| 12 | Future work | 30 | 9:45 |
| 13 | Reproducibility | 30 | 10:15 |
| 14 | Q&A intro | 30 | 10:45 |
| 15 | Thanks | 10 | 10:55 |

Leaves ~4 minutes for the actual Q&A inside the 15-minute slot.

---

*End of draft. Hand-off plan: render to `slides/final.pptx` via Marp or
manual paste-into-Keynote; replace the ASCII diagrams in slides 3 and 5
with the rendered PNGs from `docs/`.*
