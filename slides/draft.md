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
Worker agent                              Evaluator agent
     |                                          |
     |-- result via pipe --------------------- >|
     |                                          |--- quality check (LLM)
     |<-- retry signal (kill + wakeup) -------- |--- FAIL (under threshold)
     |                                          |
     |-- new result --------------------------- |--- PASS
     |                                          |
                                  forward to next stage
```

- Implemented with xv6 `kill()` + `sleep/wakeup` (xv6 has no `SIGUSR` — a generic `kill` plus the scheduler's wakeup channel is what we have to work with).
- **Bounded retry**: max 3 attempts, then surface failure to the orchestrator.
- This is a real OS-level **signaling + synchronization** problem — not a Python `if`/`while` loop.

---

## Slide 5 — Part 2: OS Adaptation (brief)

**Where the OS shows up — 10 mechanisms designed, 1 reused.**

| # | Component                     | OS concept                       | xv6 file(s) we modified                                |
|---|-------------------------------|----------------------------------|--------------------------------------------------------|
| 1 | Orchestrator                  | Process management               | `proc.h`, `proc.c` (fork-extend)                       |
| 2 | Agent → Agent transport       | IPC (pipes)                      | `pipe.c`, `user/triage.c`                              |
| 3 | Retry path                    | Signals + wakeup                 | `proc.c` (`kill` + `sleep`/`wakeup`)                   |
| 4 | Shared context guard          | Synchronization (sleeplocks)     | `console.c`, `sysproc.c` (cons_write_lock, proxy_lock) |
| 5 | Per-agent priority            | Scheduling                       | `proc.c` `scheduler()` (2-pass max-priority)           |
| 6 | Fault isolation               | Address-space isolation          | xv6 default, exercised by design                       |
| 7 | Introspection                 | System calls (5 — nos. 22–26)    | `syscall.{c,h}`, `sysproc.c`                           |
| 8 | Kernel verify + rollback (**Pattern A**) | Kernel verifier + checkpoint (3 syscalls — 27–29) | `kernel/verifier.{c,h}`, `sysproc.c`         |
| 9 | In-kernel semantic cache (**Pattern B**) | File system + system calls (3 syscalls — 30–32)   | `kernel/cache.{c,h}`, `mkfs/mkfs.c`, `sysproc.c` |
| 10 | Per-process memory quota (**T-A1**) | Memory management / resource control (1 syscall — 33) | `proc.{h,c}` (`growproc`), `sysproc.c`          |
| – | Agent context persistence     | File system (reused, read-only)  | xv6 fs (no kernel mod yet — F-01)                      |

- Rule of thumb: if it could have been `multiprocessing` or `cgroups`, we did **not** use it.
- **12 new syscalls shipped (nos. 22–33)** — from `setrole(2)` / `agentstat(2)` introspection
  through the **verify+rollback** (27–29) and **semantic-cache** (30–32) families — plus a userspace `priotest`.

**Three kernel additions that set this work apart (rows 8–10):**

- **Pattern A — in-kernel verifier (`kernel/verifier.c`, syscalls 27–29):** every LLM-proposed fix is guarded by field / range / whitelist / protected-process checks before it can take effect. **FAIL → restore (pid-tagged checkpoint) + amend the fix per the kernel's verdict code → retry; PASS → accept.** Convergence is *verdict-driven*: the correction is a causal function of *why* the kernel rejected the fix (`VERIFY_ERR_RANGE` → clamp severity, etc.), not a retry counter. The kernel, not userspace, owns the safety contract.
- **Pattern B — in-kernel response cache (`kernel/cache.c`, syscalls 30–32):** short-circuits the LLM call *before* a `PROXY_REQ` is even emitted — **FNV-1a** exact match + **MinHash/Jaccard** semantic (paraphrase) match, backed by a `/cache.bin` disk overlay. Caching becomes an OS service, not a per-script dict.
- **Per-process memory quota (`growproc` enforcement + `setquota(33)`):** the kernel caps each agent's resident memory — `sbrk` past the cap is denied (`AGENT_LOG`'d), forks inherit the cap. Per-process resource control, owned by the OS.

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
- Spearman correlation **ρ = −1.000** in the captured single shot.
- Honest disclaimer: across 3 reproductions the last pair (prio 0 vs prio 1) sometimes flips inside the same timer tick, giving ρ ≈ −0.94; the priority-respects-the-rank conclusion holds in every run (median ≈ −0.97).

---

## Slide 10 — Part 4: Demo (3/3) — Demo Walk-through & Regression

**Screenshot 6 — `triage short.log` inside xv6 shell**
- Live `PROXY_REQ` lines emitted by parser/classifier/root-cause/fix-suggester/evaluator.
- Visible `EVAL_RETRY` events for two log lines → evaluator-supervisor loop firing on real output.
- Final `TRIAGE_DONE` after bounded retries.

**Screenshot 7 — `make regression` 5/5 PASS** *(was 3-gate before Phase 10; T-90 folded the Pattern A/B tests in)*
- Gate 1: headless xv6 boot + smoke (`autotest`).
- Gate 2: shell interaction + mock proxy end-to-end (`e2e_mock` + `triage`).
- Gate 3: mock 5-stage triage round-trip.
- Gate 4: **`test_verifier.sh`** — `VERIFY_FAIL → ROLLBACK → RETRY → ACCEPT` (eval_retries=2, ok=True).
- Gate 5: **`test_cache.sh`** — `proxy_reqs_saved=2` (exact + paraphrase short-circuit).
- All green → the design is reproducible from a clean checkout, not just a one-off demo. (Bench script also exposes a 6th evidence gate via `bench/capture_patterns.py` → `docs/patterns_demo.txt`: `VERIFY_FAIL=2 ROLLBACK=2 RETRY=2 ACCEPT=5 CACHE_HIT=2`.)

**Talking-point summary**
- Five LLM agents, each a real xv6 process.
- One evaluator, one priority scheduler, twelve new syscalls (nos. 22–33) — including kernel verify+rollback, a semantic response cache, and a per-process memory quota.
- Reproducible, measurable, and isolated — at the OS level, by construction.

---

## Backup / Q&A — Limitations and What's Next

- **Filesystem writes are limited to the kernel semantic cache** (`/cache.bin`, via the inode API). Agent-context persistence through a richer fs-backed mechanism — checkpoint/resume of an in-flight pipeline, swap-out of an idle agent — is still tracked as **F-01**.
- **Live-mode benchmark** at `BENCH_N=5` is still a human-run step (T-62); reported numbers in this deck use `replay`/`mock` for reproducibility.
- **`retry_context` is recorded, not yet re-fed.** The host accumulates each evaluator violation reason into `injected_sample` / `retry_context` JSON fields, but it is *not* threaded back into the model prompt yet — partly because line-accumulated `PROXY_RES` frames can desync the wire format, partly because the kernel cache would short-circuit the re-issued prompt. Proper fix is guest-side embedding of the reason into the next prompt (STATUS §5 future task).
- **Single-host scope**: no multi-node scheduling; agents share one xv6 guest.
- **Future work**:
  - Planner-executor agents on top of the current static DAG.
  - Proxy multiplexing for higher live throughput.
  - RAG / ReAct loops as additional agent roles.

**Companion artifacts in the repo** (GuideLine §5 deliverables #2 / #3):
- `docs/TECHNICAL_REPORT.md` — system architecture, OS-concept mapping in depth (incl. §3 row 8/9 and §10.8 for Patterns A/B).
- `PROCESS.md` — weekly progress, design decisions (D-01..D-10), issue ledger (I-01..I-11).
- Operational ledger: `STATUS.md §3` (T-NN queue), `STATUS.md §5` (blocker history).

---

*End of draft.*
