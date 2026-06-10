# Liberal_OS — Final Presentation Draft

> Direction A (OS for LLM) · Presenter: Lee Taejin · ~15 min, 10 slides
> English-only outline. Reviewed before HTML conversion.

---

## Slide 1 — Title

**Liberal_OS: An xv6-Based Mini-OS for Orchestrating LLM Agents**

- Team: Liberal_OS
- Presenter: Lee Taejin
- Direction: **A — OS for LLM**
- Backend: Upstage Solar Pro 3 (OpenAI-compatible)
- Repo: github.com/HANJUSON/Liberal_OS

---

## Slide 2 — Problem & Direction (1 min)

**Why "OS for LLM" and not another LLM wrapper?**

- Multi-agent LLM systems today are mostly Python scripts on Linux.
  - The OS sees them as one opaque process tree.
  - No isolation guarantees, no priority for critical work, no quota enforcement.
- Our claim: **agent orchestration is an OS problem.**
  - Each agent should be a first-class kernel-managed entity.
  - Scheduling, IPC, isolation, signaling must come from the kernel — not from a userspace library.
- We modified the **xv6 kernel itself**, not the userspace around it.

> Frames the work against the "thin wrapper" criterion in the project guideline.

---

## Slide 3 — Part 1: LLM Architecture Overview

**Five cooperating agents, one supervisor.**

```
[Raw log stream]
      |
      v
   parser   --pipe-->  classifier  --pipe-->  root-cause
                                                  |
                                                  v
                                            fix-suggester
                                                  |
                                                  v
                                          +-->  evaluator  --retry signal--+
                                          |       |                        |
                                          |       v                        |
                                          |   [Triage report]              |
                                          +--------------------------------+
```

- **parser** — turns one raw log line into a structured record.
- **classifier** — assigns level (INFO / WARN / ERROR / CRITICAL).
- **root-cause** — runs only on ERROR/CRITICAL.
- **fix-suggester** — proposes a remediation.
- **evaluator** — quality-checks the previous output and can demand retries.

All five call the same backend: **Upstage Solar Pro 3** through a Proxy Daemon.

---

## Slide 4 — Part 1 (cont.): Supervisor Pattern

**Why an evaluator? — LLM output is non-deterministic.**

```
Worker agent                          Evaluator agent
     |                                      |
     |-- result via pipe ------------------>|
     |                                      |--- quality check (LLM)
     |<-- retry signal (kill/SIGUSR) -------|--- fail (under threshold)
     |                                      |
     |-- new result ----------------------->|--- pass
     |                                      |
                                  forward to next stage
```

- Implemented with xv6 `kill()` + `sleep/wakeup`.
- **Bounded retry**: max 3 attempts, then surface failure to the orchestrator.
- This is a real OS-level **signaling + synchronization** problem — not a Python `if`/`while` loop.

---

## Slide 5 — Part 2: OS Adaptation (brief)

**Where the OS shows up — 7 mechanisms designed, 1 reused.**

| # | Component                | OS concept              | xv6 file(s) we modified           |
|---|--------------------------|-------------------------|-----------------------------------|
| 1 | Orchestrator             | Process management      | `proc.h`, `proc.c` (fork-extend)  |
| 2 | Agent → Agent transport  | IPC (pipes)             | `pipe.c`                          |
| 3 | Retry path               | Signals + wakeup        | `proc.c`, `trap.c`                |
| 4 | Shared context guard     | Synchronization (locks) | `spinlock.c`                      |
| 5 | Per-agent priority       | Scheduling              | `sched.c` (priority queue)        |
| 6 | Fault isolation          | Address-space isolation | xv6 default, exercised by design  |
| 7 | Introspection            | New system calls        | `syscall.c/h`, `sysproc.c`        |
| – | Agent context persistence| File system (reused)    | xv6 fs (no kernel mod yet — F-01) |

- Rule of thumb: if it could have been `multiprocessing` or `cgroups`, we did **not** use it.
- **11 new syscalls shipped (nos. 22–32)** — from `setrole(2)` / `agentstat(2)` introspection
  through the **verify+rollback** (27–29) and **semantic-cache** (30–32) families — plus a userspace `priotest`.

**Two kernel-level patterns that set this work apart:**

- **Pattern A — in-kernel verifier (`kernel/verifier.c`, syscalls 27–29):** every LLM-proposed fix is guarded by field / range / whitelist / protected-process checks before it can take effect. **FAIL → checkpoint/restore rollback + retry; PASS → accept.** The kernel, not userspace, owns the safety contract.
- **Pattern B — in-kernel response cache (`kernel/cache.c`, syscalls 30–32):** short-circuits the LLM call *before* a `PROXY_REQ` is even emitted — **FNV-1a** exact match + **MinHash/Jaccard** semantic (paraphrase) match, backed by a `/cache.bin` disk overlay. Caching becomes an OS service, not a per-script dict.

---

## Slide 6 — Part 2 (cont.): Host–Guest Boundary

**xv6 has no network stack — so where does the LLM call happen?**

```
+------------------------ QEMU host (Linux) ------------------------+
|                                                                   |
|   +----------- xv6 guest ------------+   +-- Proxy Daemon (py) -+ |
|   |  orchestrator + 5 agent procs    |   |  - Upstage API call  | |
|   |  (kernel-managed, pipe-linked)   |<->|  - retry + cache     | |
|   |                                  |   |  - replay/mock modes | |
|   +----------------------------------+   +----------------------+ |
|                  virtio serial / pipe                             |
+-------------------------------------------------------------------+
```

- xv6 only sees a `read` / `write` to a pipe — the LLM is abstracted away.
- The daemon supports three modes:
  - `live` — real Upstage API.
  - `replay` — cached responses (used for reproducible measurement).
  - `mock` — deterministic stub (CI and demo without network).

---

## Slide 7 — Part 3: When to Use Liberal_OS

**This project is useful when you need…**

- **Post-hoc triage of a high-volume event/log stream** that benefits from staged LLM reasoning (extract → classify → diagnose → fix).
- **Strong fault isolation** between agents: one agent crashing must not bring the pipeline down.
  - xv6 process isolation gives this for free; a single Python process does not.
- **Priority differentiation** between work items (e.g., CRITICAL logs must preempt INFO).
  - Our modified scheduler enforces this at the kernel level — userspace cannot bypass it.
- **A platform-level retry/quality contract** rather than per-script ad-hoc loops.
  - The evaluator-supervisor + signal-based retry is a reusable mechanism, not a one-off.
- **Predictable, replayable measurement** of an agent pipeline (the `replay` mode + cache).

**Not a fit for:** real-time chat assistants, single-agent toolformer flows, or any workload that fits comfortably in one process.

---

## Slide 8 — Part 4: Demo (1/3) — End-to-End Triage

**Screenshot 1 — Upstage connectivity proof**
- `MODE=live python3 host/hello_upstage.py`
- Round-trip ~1.07 s vs. 0.05 s in mock → real inference, not a stub.

**Screenshot 2 — 25-call full-stack triage**
- `python3 host/proxy_daemon.py --triage short.log --mode replay`
- 5 log lines × 5 agent stages = 25 LLM calls, all served.
- Result line: `served:{parser:5, classifier:5, rootcause:5, fixsuggest:5, evaluator:5}, eval_oks:5, eval_fails:0`.

**Screenshot 3 — per-call trace**
- `out/live-trace.log` shows each PROXY_REQ tagged with stage and pid.
- Demonstrates that the kernel really did spawn distinct agent processes.

---

## Slide 9 — Part 4: Demo (2/3) — OS-Level Visibility

**Screenshot 4 — `agentstat` system call**
- New syscall returns a JSON snapshot of every process: `pid`, `name`, `role`, `prio`, `st`.
- Example baseline: `[{"pid":1,"name":"init","role":"none",...}, ...]`
- This is **kernel-side introspection** — userspace `ps` could not produce the `role`/`prio` fields without our `proc` extension.

**Screenshot 5 — `priotest` priority scheduling**
- 6 processes spawned with descending priorities (5 → 0).
- Completion order tracked: `DONE i=0 prio=5 t=0` first, `DONE i=5 prio=0 t=0` last.
- Spearman correlation **ρ = −1.000** between priority and completion rank → priority scheduler is doing what we designed.

---

## Slide 10 — Part 4: Demo (3/3) — Demo Walk-through & Regression

**Screenshot 6 — `triage short.log` inside xv6 shell**
- Live `PROXY_REQ` lines emitted by parser/classifier/root-cause/fix-suggester/evaluator.
- Visible `EVAL_RETRY` events for two log lines → evaluator-supervisor loop firing on real output.
- Final `TRIAGE_DONE` after bounded retries.

**Screenshot 7 — `make regression` 3/3 PASS**
- Gate 1: headless xv6 boot + smoke.
- Gate 2: shell interaction (commands echo, basic syscalls).
- Gate 3: mock proxy end-to-end.
- All green → the design is reproducible from a clean checkout, not just a one-off demo.

**Talking-point summary**
- Five LLM agents, each a real xv6 process.
- One evaluator, one priority scheduler, eleven new syscalls (nos. 22–32) — including kernel verify+rollback and a semantic response cache.
- Reproducible, measurable, and isolated — at the OS level, by construction.

---

## Backup / Q&A — Limitations and What's Next

- **Filesystem usage is shallow** (read-only context loading). Persistent agent state via a new fs-backed mechanism is tracked as **F-01**.
- **Live-mode benchmark** at `BENCH_N=5` is still a human-run step (T-62); reported numbers in this deck use `replay`/`mock` for reproducibility.
- **Single-host scope**: no multi-node scheduling; agents share one xv6 guest.
- **Future work**:
  - Planner-executor agents on top of the current static DAG.
  - Proxy multiplexing for higher live throughput.
  - RAG / ReAct loops as additional agent roles.

---

*End of draft.*
