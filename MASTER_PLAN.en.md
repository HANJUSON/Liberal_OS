# Master Plan — Liberal_OS (xv6-based LLM Multi-Agent OS)

> This document is the single source of truth (SSoT) that consolidates
> the project's **design decisions** and **work schedule**. It absorbs
> the prior `ImplementationPlan.md` (decision record) and `TASKS.md`
> (work queue).
>
> It also serves as the integrated first draft for Deliverable #2
> (Technical Report) and #3 (Development Process Document) of §5 of
> the GuideLine.

---

## Table of Contents

**Part I — Plan (Design Decisions)**
1. [Project Overview](#1-project-overview)
2. [Killer Scenario — Log Triage Pipeline](#2-killer-scenario--log-triage-pipeline)
3. [System Architecture](#3-system-architecture)
4. [Implementation Method Decisions](#4-implementation-method-decisions)
5. [LLM Backend — Upstage Solar Pro 3](#5-llm-backend--upstage-solar-pro-3)
6. [Evaluation Plan](#6-evaluation-plan)
7. [Schedule Overview — Week 10~14](#7-schedule-overview--week-1014)
8. [Deliverable Mapping (Guideline §5)](#8-deliverable-mapping-guideline-5)
9. [Risks and Mitigation](#9-risks-and-mitigation)
10. [Role Assignment](#10-role-assignment)

**Part II — Work Schedule (T-NN Task Queue)**
11. [Task Format and State Notation](#11-task-format-and-state-notation)
12. [Phase 1 — Harness Bootstrap](#12-phase-1--harness-bootstrap-week-10)
13. [Phase 2 — xv6 proc Extension + virtio](#13-phase-2--xv6-proc-extension--virtio-early-week-11)
14. [Phase 3 — Five Agents + IPC pipe](#14-phase-3--five-agents--ipc-pipe-late-week-11--early-week-12)
15. [Phase 4 — Evaluator Retry Loop](#15-phase-4--evaluator-retry-loop-mid-week-12)
16. [Phase 5 — Scheduler Modification](#16-phase-5--scheduler-modification-late-week-12-🔴)
17. [Phase 6 — Evaluation](#17-phase-6--evaluation-week-13)
18. [Phase 7 — Deliverables](#18-phase-7--deliverables-week-1314)
19. [Progress Summary](#19-progress-summary)
20. [Rules for Adding Tasks](#20-rules-for-adding-tasks)

**Appendices**
- [Appendix A — Implementation Methods Not Adopted](#appendix-a--implementation-methods-not-adopted)
- [Appendix B — Self-Checklist (Guideline §3 README Requirements)](#appendix-b--self-checklist-guideline-3-readme-requirements)

---

# Part I — Plan (Design Decisions)

## 1. Project Overview

### 1.1 One-Paragraph Summary (Guideline §3 requirement)

The **Liberal_OS** team implements a system that orchestrates multiple
LLM agents through **OS-level isolation, scheduling, and resource
management mechanisms** by directly modifying the xv6 kernel. Each
agent (parser / classifier / root-cause / fix-suggester / evaluator) is
represented as an xv6 process; the orchestrator performs inter-agent
communication via a directly-implemented IPC (pipe) and assigns
per-agent priorities through a directly-modified scheduler. The
Evaluator agent implements the Supervisor pattern: it verifies the
output of Worker agents and sends retry signals when quality is below
threshold. Actual LLM API calls are handled by a Proxy Daemon on the
QEMU host; from xv6's perspective, an LLM call is abstracted as an
IPC request. This project focuses on **treating the LLM API as a
resource directly managed by the xv6 kernel** — it does not call
existing OS libraries but designs and implements OS concepts themselves.

### 1.2 Chosen Direction: **A (OS for LLM)**

Corresponds to direction A in §2 of the guideline — "an OS, runtime
layer, or agent platform that hosts, serves, and orchestrates LLMs."
Specifically, it falls under the category of **"a mini-OS that manages
multiple concurrent LLM processes (agents) by fairly allocating
CPU / memory / tool quotas."**

### 1.3 Relationship with Other Documents

| Document | Role |
|---|---|
| `README.md` | OS mechanism **catalog**. Reused as a technical appendix. |
| `CLAUDE.md` | Claude Code operating manual. References this document as the SSoT. |
| `HARNESS.md` | CC autonomous-operation harness design. Uses §11~§20 of this document as the task queue. |
| **This document (`MASTER_PLAN.md`)** | **Decision, execution, and schedule layer.** Fully rewritten on the xv6 basis; absorbs the task queue. |

---

## 2. Killer Scenario — Log Triage Pipeline

### 2.1 Reasons for Adoption

§2 of the guideline lists the **mandatory constraint** that "an OS
component must do more than simply 'an LLM runs on top of it.'" Log
triage qualifies as a **substantive** xv6 OS decision for the
following reasons.

- **Large input**: handling thousands of log lines naturally justifies
  parallelization (speedup of 2x+).
- **Need for isolation**: a failure parsing a single log line must not
  kill the entire pipeline → xv6 **process isolation** is a real
  requirement, not decoration.
- **Need for quality verification**: because LLM outputs are
  non-deterministic, an Evaluator must verify them and command retries
  → the **Supervisor pattern** carries functional meaning.
- **Priorities**: critical-level logs must be processed first → xv6
  **scheduler modification** has clear value.

### 2.2 Agent Composition

```
[Raw Log Stream]
       │
       ▼
┌────────────────┐
│ parser         │  log line → structured record
│  (xv6 proc)   │  (Upstage Solar Pro 3 via Proxy)
└────────┬───────┘
         │ pipe
         ▼
┌────────────────┐
│ classifier     │  level / category classification
│  (xv6 proc)   │  (INFO/WARN/ERROR/CRITICAL)
└────────┬───────┘
         │ pipe
         ▼
┌────────────────┐
│ root-cause     │  diagnose only ERROR / CRITICAL
│  (xv6 proc)   │
└────────┬───────┘
         │ pipe
         ▼
┌────────────────┐
│ fix-suggester  │  generate fix suggestion
│  (xv6 proc)   │
└────────┬───────┘
         │ pipe
         ▼
┌────────────────┐     retry signal (pipe)
│  evaluator     │ ──────────────────────▶ (feedback to the relevant agent)
│  (xv6 proc)   │  quality verification + retry decision
└────────┬───────┘
         │
         ▼
   [Triage Report]
```

Five agent process types in total. The Evaluator verifies the output
of each stage and, when quality is below threshold, sends a retry
signal to the corresponding agent.

### 2.3 Input / Output Definition

- **Input**: log file passed from the host Linux to the xv6 guest
  (via virtio/pipe).
- **Output**: structured triage results (stored in the xv6 filesystem).
- **Demo scenario**: run `triage kernel-dmesg.log` in the xv6 shell →
  agents process in parallel → AgentMonitor shows real-time metrics →
  the report is produced.

---

## 3. System Architecture

### 3.1 Host-Guest Separation Architecture

xv6 has no network stack, so LLM API calls are handled by a Proxy
Daemon on the QEMU host.

```
┌──────────────────────────────────────────────────────┐
│                  QEMU Host (Linux)                   │
│                                                      │
│  ┌───────────────────────────────┐  ┌─────────────┐  │
│  │         xv6 Guest             │  │ LLM Proxy   │  │
│  │                               │  │ Daemon      │  │
│  │  ┌─────────────────────────┐  │  │             │  │
│  │  │   Orchestrator (proc 1) │  │  │ - API call  │  │
│  │  │   - fork agents         │◄─┼─►│ - return    │  │
│  │  │   - scheduler priority  │  │  │ - queue mgmt│  │
│  │  │   - signal handler      │  │  └─────────────┘  │
│  │  └────────────┬────────────┘  │        ▲          │
│  │               │ fork          │        │          │
│  │    ┌──────────┼──────────┐    │        │          │
│  │    ▼          ▼          ▼    │        │          │
│  │  [parser] [classif.] [r-c]   │        │          │
│  │     │          │        │    │        │          │
│  │     └────pipe──┴──pipe──┘    │        │          │
│  │               │              │        │          │
│  │          [evaluator] ────────┼────────┘          │
│  │               │              │   virtio/pipe      │
│  │          [fix-sugg.]         │                   │
│  └───────────────────────────────┘                  │
└──────────────────────────────────────────────────────┘
```

From xv6's perspective, an LLM call is abstracted as **an IPC request
sent to the Proxy Daemon**.

### 3.2 OS Concept Mapping Table

To satisfy the guideline §2 requirement that "OS concepts be included
substantively," the following OS concepts are **designed and
implemented directly in the xv6 kernel.**

| # | Component | OS Concept | xv6 Implementation Means |
|---|---|---|---|
| 1 | Orchestrator | **Process management** | xv6 `fork()`, extension of `proc` struct |
| 2 | Inter-agent communication | **IPC (pipe)** | direct implementation/modification of xv6 `pipe` |
| 3 | Evaluator ↔ Worker | **IPC (signals)** | xv6 `kill()`, `signal` mechanism |
| 4 | Shared context | **Synchronization** | xv6 `spinlock`, `sleep/wakeup` |
| 5 | Agent priority | **Scheduling** | xv6 scheduler modification (add priority queue) |
| 6 | Fault isolation | **Process isolation** | xv6 per-process independent address space |
| 7 | Context save | **Filesystem** | save agent state to xv6 filesystem |
| 8 | Monitoring | **System call** | add xv6 system call (`agentstat`) |

> All of the above are implemented by **directly modifying and adding
> to the xv6 kernel code**, not by **calling** Python libraries or
> Linux features. This is the fundamental difference from the prior
> plan (which leveraged multiprocessing, cgroups).

### 3.3 Supervisor Pattern (Evaluator Loop)

```
Worker agent                       Evaluator agent
      │                                  │
      │──── deliver result (pipe write) ─▶│
      │                                  │ quality verification
      │                                  │ (Upstage API call)
      │◀─── retry signal (kill/signal) ──│ if below threshold
      │                                  │
      │ re-execute                       │
      │──── deliver new result ──────────▶│
      │                                  │ pass
      │                            to next stage
```

Implemented with xv6's `kill()` + `sleep/wakeup` mechanism. The
Evaluator instructs the Worker to retry, and the Worker re-executes
upon receiving the signal.

### 3.4 Sequence Diagram

```
Shell   Orchestrator  pipe(1→2)  Parser   pipe(2→3)  Classifier  Evaluator  Proxy(Host)
  │          │            │        │           │           │          │           │
  │─triage──▶│            │        │           │           │          │           │
  │          │─fork───────────────▶│           │           │          │           │
  │          │─fork────────────────────────────────────────────────────▶│          │
  │          │            │        │           │           │          │           │
  │          │─write──────▶│       │           │           │          │           │
  │          │            │        │─read──────│           │          │           │
  │          │            │        │─LLM req───────────────────────────────────────▶│
  │          │            │        │◀──LLM res─────────────────────────────────────│
  │          │            │        │─write──────────────────▶│         │           │
  │          │            │        │           │             │─read───▶│           │
  │          │            │        │           │             │         │─verify────▶│
  │          │            │        │           │             │         │◀──result───│
  │          │            │        │           │             │         │            │
  │          │            │        │◀──signal(retry)─────────────────── │           │
  │          │            │        │ re-execute│             │          │           │
  │          │◀─────────────────────────────────────────────────────────│           │
  │◀─report──│            │        │           │             │          │           │
```

---

## 4. Implementation Method Decisions

### 4.1 Scope of xv6 Kernel Modification

The core of the implementation is directly modifying the xv6 source.
Target files and contents:

| xv6 File | Modification |
|---|---|
| `proc.h` | add `agent_role`, `priority`, `agent_state` fields to the `proc` struct |
| `proc.c` | modify `fork()` — inherit agent role, initialize priority |
| `sched.c` | modify scheduler — Round Robin → priority-based scheduling |
| `pipe.c` | adjust pipe buffer size for inter-agent communication; verify blocking behavior |
| `spinlock.c` | verify and, if needed, extend the lock used for shared-context access |
| `syscall.c/h` | add `agentstat` system call (for querying agent state) |
| `file.c` | save/restore agent context to/from the filesystem |

### 4.2 QEMU Host-Guest Communication

The xv6 guest and the Linux host Proxy Daemon communicate over a
virtio serial or a shared pipe.

```
xv6 agent             virtio/pipe          Linux Proxy Daemon
      │                                          │
      │─── LLM request (serialized struct) ─────▶│
      │                                          │─── Upstage API call
      │                                          │◀── API response
      │◀── LLM response (serialized struct) ─────│
```

From xv6's perspective, this is just read/write system calls. xv6
does not need to know that an LLM exists.

### 4.3 Methods Not Adopted

| Method | Reason for non-adoption |
|---|---|
| Python multiprocessing | **Calls** OS features — does not meet the "directly implemented" condition |
| Use Linux cgroups | **Uses** Linux features — does not meet the "directly designed" condition |
| Linux threading | Same reason |

---

## 5. LLM Backend — Upstage Solar Pro 3

### 5.1 Proxy Daemon Structure

xv6 cannot make direct HTTP calls. A Proxy Daemon on the Linux host
relays them.

```python
# host/proxy_daemon.py (runs on the Linux host)
import os, serial
from openai import OpenAI

client = OpenAI(
    api_key=os.environ['UPSTAGE_API_KEY'],
    base_url='https://api.upstage.ai/v1',
)

def handle_request(prompt: str, system: str) -> str:
    response = client.chat.completions.create(
        model='solar-pro',
        max_tokens=1000,
        messages=[
            {'role': 'system', 'content': system},
            {'role': 'user', 'content': prompt},
        ],
    )
    return response.choices[0].message.content

# Communicate with xv6 via virtio serial or a named pipe.
# Request: JSON-serialized {role, prompt}
# Response: JSON-serialized {result}
```

### 5.2 API Key Management

```bash
# .env (NEVER commit to git)
UPSTAGE_API_KEY=up-xxxxxxxxxxxxxxxxxxxx

# .gitignore
.env
.env.*
*.key
out/
.cache/llm/
```

### 5.3 Rate Limit Handling — Exponential Backoff + Cache

```python
# host/proxy_daemon.py
import hashlib, pickle, pathlib, time, random

CACHE_DIR = pathlib.Path('.cache/llm')
CACHE_DIR.mkdir(parents=True, exist_ok=True)

def cached_llm_call(system: str, user: str) -> str:
    key = hashlib.sha256(f'{system}|{user}'.encode()).hexdigest()
    cache_file = CACHE_DIR / f'{key}.pkl'
    if cache_file.exists():
        return pickle.loads(cache_file.read_bytes())
    for attempt in range(6):
        try:
            response = client.chat.completions.create(
                model='solar-pro',
                max_tokens=1000,
                messages=[
                    {'role': 'system', 'content': system},
                    {'role': 'user', 'content': user},
                ],
            )
            result = response.choices[0].message.content
            cache_file.write_bytes(pickle.dumps(result))
            return result
        except Exception as e:
            if 'rate' not in str(e).lower() and attempt > 2:
                raise
            time.sleep((2 ** attempt) + random.random())
    raise RuntimeError('rate limit: exhausted retries')
```

**Evaluation mode options**:
- `--mode live`: actual API calls (for demos).
- `--mode replay`: cache-only (for measurement, guarantees reproducibility).
- `--mode mock`: `time.sleep` + dummy response (for unit tests).

---

## 6. Evaluation Plan

### 6.1 Evaluation Metrics

| Metric | Measurement Method | Target | Repetitions |
|---|---|---|---|
| **Parallel processing effect (speedup)** | sequential wall-clock / parallel wall-clock | **≥ 2x** | 5-run mean ± stdev |
| **Fault isolation** | survival ratio of remaining agents after killing one | **100%** | 10 times |
| **Evaluator retry effect** | output quality score, before vs. after retry | observable quality gain | 20-sample basis |
| **Priority scheduling effect** | processing-time difference between CRITICAL and INFO logs | asymmetric distribution observed | 1 run (60 s) |
| **Pre/post scheduler modification comparison** | Round Robin vs. priority scheduler processing time | quantitative report | 5-run mean |

### 6.2 Controlled Variables

- **LLM response**: `--mode replay` (use cached responses only).
- **Input log**: a fixed 100-line log set.
- **Number of agents**: 5 (parser/classifier/root-cause/fix-suggester/evaluator).
- **Hardware**: same machine, fixed QEMU configuration.
- **Repetitions**: each experiment is repeated 5+ times; mean and stdev reported.

### 6.3 Measurement Automation

```bash
#!/usr/bin/env bash
# bench/run_all.sh

echo "[1/5] sequential baseline..."
for i in 1 2 3 4 5; do
  ./bench sequential --mode replay > out/bench/seq_$i.json
done

echo "[2/5] parallel (xv6 multi-process)..."
for i in 1 2 3 4 5; do
  ./bench parallel --mode replay > out/bench/par_$i.json
done

echo "[3/5] fault isolation..."
./bench fault_isolation --iterations 10 > out/bench/fault.json

echo "[4/5] evaluator quality improvement..."
./bench evaluator_quality --samples 20 > out/bench/eval_quality.json

echo "[5/5] scheduler priority effect..."
./bench priority --duration 60 > out/bench/priority.json

./bench report out/bench/ > out/REPORT.md
echo "Done: out/REPORT.md"
```

---

## 7. Schedule Overview — Week 10~14

> Definition of Done per week. Per-task (T-NN) detail is in Part II.

### Week 10 — System Sketch

- [ ] All team members review this document and finalize §2 (the scenario).
- [ ] xv6 build environment set up (QEMU + xv6 source cloned, build verified).
- [ ] Block diagram (§3.1) drawn with draw.io; PNG committed to the repo.
- [ ] Proxy Daemon `hello-upstage.py` — Upstage API call verified.
- [ ] Submit the **official one-paragraph proposal** (§1.1) to the instructor.
- [ ] Restructure the repository README.md according to the guideline §3 format.
- [ ] **Finalize role assignment** (§10 of this document).

### Week 11 — MVP

- [ ] Add `agent_role`, `priority` fields to xv6 `proc.h`.
- [ ] Spawn 3 agent processes via `fork()` in xv6, connected by pipes.
- [ ] End-to-end virtio communication between Proxy Daemon ↔ xv6 working.
- [ ] Parser agent calls Upstage API through the Proxy and returns the result.
- [ ] Kill one agent → Orchestrator survives.
- [ ] CI passes with `--mode mock`.

### Week 12 — Integration + Scheduler Modification

- [ ] All 5 agents operational (parser/classifier/root-cause/fix-suggester/evaluator).
- [ ] xv6 scheduler modification complete — priority-queue based.
- [ ] Evaluator ↔ Worker retry loop working (`kill` + `sleep/wakeup`).
- [ ] `agentstat` system call added and verified.
- [ ] AgentMonitor running; metrics recorded.
- [ ] **English slide skeleton** started (at least the table of contents).

### Week 13 — Experiments + Report + Dry Run

- [ ] `bench/run_all.sh` executed; `out/REPORT.md` generated.
- [ ] All 5 experiments meet targets, or root-cause analysis for those that miss.
- [ ] **Technical Report** complete.
- [ ] **Development Process Document** (`PROCESS.md`) complete.
- [ ] **English slides** first cut complete.
- [ ] One dry run (15-minute presentation timed).

### Week 14 — Final Presentation

- [ ] Final slides; demo GIF / video.
- [ ] Presentation (Professor 15% + Peer review 15%).
- [ ] Repository README finalized; demo screenshots added.

---

## 8. Deliverable Mapping (Guideline §5)

| # | Guideline §5 Deliverable | Location in This Project |
|---|---|---|
| 1 | **Application** | xv6 modified source (`xv6-src/`), Proxy Daemon (`host/`), `bench/`, demo GIF (`docs/demo.gif`) |
| 2 | **Technical Report** | `docs/TECHNICAL_REPORT.md` + `out/REPORT.md` (experimental results) |
| 3 | **Development Process Document** | `PROCESS.md`, `docs/meetings/` folder |
| 4 | **Presentation Slides** (English) | `slides/final.pptx` (begun in Week 12, finalized in Week 14) |

---

## 9. Risks and Mitigation

| Risk | Signal | Mitigation |
|---|---|---|
| **No network stack in xv6** | xv6 cannot call APIs directly | Resolved by the §3.1 Host-Guest architecture; Proxy Daemon relays. |
| **virtio communication complexity** | xv6 ↔ host communication not working by Week 11 | Start with simple serial via QEMU `-chardev`; fall back to a shared filesystem (temporary) if it fails. |
| **"Thin-wrapper" misreading** | The report reads as "the Proxy made the API call." | Place the §3.2 OS mapping table at the top of the Technical Report; present xv6 kernel modification commits as evidence. |
| **xv6 scheduler modification complexity** | Scheduler not done by Week 12 | Keep Round Robin and measure only via `agentstat` first; add priority in Week 13. |
| **LLM non-determinism** | Speedup measurements vary each run | Use `--mode replay` to use cached responses only; live API only at demo time. |
| **Evaluator retry loop infinite repetition** | Infinite retries when quality threshold is missed | Hard-code a maximum retry count (3); on overflow, report failure to the Orchestrator. |
| **English presentation delays** | No slides by Week 13 | Start Week 12 by writing code comments and key report sentences in English. |

---

## 10. Role Assignment

> **TBD — to be finalized at the Week 10 meeting**

Per §1 of the guideline, a 4-person team. Recommended split:

| Role | Responsibility Area | Key Deliverables |
|---|---|---|
| **Team Leader / Orchestration** | single point of contact, xv6 Orchestrator process, schedule management | schedule, meeting minutes, integrated build |
| **xv6 Kernel** | modifications to `proc.h`, `sched.c`, `spinlock`, `syscall` | items 1, 4, 5, 6, 8 of the §3.2 mapping table |
| **Agents / LLM** | Proxy Daemon, 5-agent implementation, IPC pipe wiring | all of §5, agent modules, virtio communication |
| **Evaluation / Monitoring** | AgentMonitor, `bench/run_all.sh`, English slides | all of §6, experimental results, `slides/` |

If the 3-person team exception applies, merge Evaluation/Monitoring into the xv6 Kernel role.

---

# Part II — Work Schedule (T-NN Task Queue)

> Basis document for Claude Code's autonomous decision of "what to do
> next." Per `CLAUDE.md` §2 (Work Loop), only one task may be in the
> `[~]` state at a time across all worktrees.

## 11. Task Format and State Notation

### 11.1 State Notation
- `[ ]` **TODO** — not yet started.
- `[~]` **IN PROGRESS** — CC is working on it (only one across all worktrees).
- `[x]` **DONE** — verification passed, commit complete.
- `[!]` **BLOCKED** — see `BLOCKED.md`. Human intervention required.
- 🔴 **HUMAN GATE** — explicit human approval required before starting.

### 11.2 Task Metadata
Every task carries the following metadata:
- **depends**: list of predecessor task IDs. All must be `[x]` before
  this task can start.
- **files**: files this task is allowed to touch. Modifying any other
  file requires going `[!]` BLOCKED.
- **verify**: verification command. Must pass before flipping to `[x]`.
- **estimate**: expected time (in CC autonomous time).
- **assignee**: owning worktree (when 4-person team + 4-worktree
  operation is in effect).

---

## 12. Phase 1 — Harness Bootstrap (Week 10)

> Goal: build the foundational infrastructure for CC autonomous
> operation. Once this Phase is done, subsequent phases run mostly
> autonomously.

### T-01 `[x]` Author `.claude/settings.json`
- **depends**: none
- **files**: `.claude/settings.json`
- **verify**: after restarting CC, confirm that `sudo` is rejected.
- **estimate**: 10 min
- **assignee**: harness
- **note**: configure allowed_tools / disallowed_tools. Block `sudo`,
  `git push`, `rm -rf /`.

### T-02 `[x]` Set up `.env.example` and `.gitignore`
- **depends**: none
- **files**: `.env.example`, `.gitignore`
- **verify**: confirm `.env` is not tracked by git (`git check-ignore .env`).
- **estimate**: 10 min
- **assignee**: harness

### T-03 `[x]` Verify xv6 build environment
- **depends**: none
- **files**: none (verification only)
- **verify**: `cd xv6-src && make qemu` reaches the xv6 shell prompt
  `$ `; exit cleanly with `Ctrl-A X`.
- **estimate**: 30 min
- **assignee**: harness

### T-04 `[x]` Host-side `hello-upstage.py`
- **depends**: T-02
- **files**: `host/hello_upstage.py`, `host/requirements.txt`
- **verify**: `MODE=live python host/hello_upstage.py` → one-line
  solar-pro response.
- **estimate**: 30 min
- **assignee**: agent

### T-05 `[x]` `tests/autotest.sh` skeleton
- **depends**: T-03
- **files**: `tests/autotest.sh`, `tests/inputs/smoke.in`, `Makefile`
- **verify**: `make autotest` prints `PASS` or `FAIL` within 60 s.
- **estimate**: 2 h
- **assignee**: harness
- **note**: headless QEMU + serial capture + grep verdict. Requires a
  `smoketest` userland program in xv6.

### T-06 `[x]` Add xv6 user `smoketest.c`
- **depends**: T-03
- **files**: `xv6-src/user/smoketest.c`, `xv6-src/Makefile`
- **verify**: running `smoketest` in the xv6 shell prints
  `SMOKE_TEST_PASS`.
- **estimate**: 30 min
- **assignee**: kernel
- **note**: verify basic fork/wait/pipe behavior, then print PASS.

### T-07 `[x]` `tests/regression.sh` skeleton
- **depends**: T-05, T-06
- **files**: `tests/regression.sh`
- **verify**: `make regression` → autotest + basic shell command
  check + Proxy hello.
- **estimate**: 1 h
- **assignee**: harness

### T-08 `[x]` `bench/summarize.py` skeleton
- **depends**: T-02
- **files**: `bench/summarize.py`
- **verify**: given 5 dummy JSON inputs, emit one-line JSON with mean
  and stdev.
- **estimate**: 30 min
- **assignee**: bench

### T-09 `[x]` Add panic-dump macro
- **depends**: T-03, T-06
- **files**: `xv6-src/kernel/printf.c`, `xv6-src/kernel/proc.h`
- **verify**: intentionally panic; confirm
  `[PANIC_DUMP_BEGIN]...[PANIC_DUMP_END]` is emitted.
- **estimate**: 1 h
- **assignee**: kernel

### T-10 `[x]` Introduce `AGENT_LOG` macro
- **depends**: T-09
- **files**: `xv6-src/kernel/agent_log.h`, `xv6-src/kernel/defs.h`
- **verify**: calling `AGENT_LOG("info", "test %d", 42)` from the
  kernel emits `[AGENT][info][...]`.
- **estimate**: 30 min
- **assignee**: kernel

---

## 13. Phase 2 — xv6 proc Extension + virtio (Early Week 11)

> Goal: embed agent metadata in the xv6 `proc` struct and secure a
> channel for host communication.

### T-20 `[x]` Extend `proc` struct
- **depends**: T-06, T-10
- **files**: `xv6-src/kernel/proc.h`, `xv6-src/kernel/proc.c`
- **verify**: after `fork()`, the child's `agent_role`, `priority`
  fields are inherited from the parent (test:
  `xv6-src/user/test_proc_fields.c`).
- **estimate**: 2 h
- **assignee**: kernel
- **note**: defaults are `agent_role = "none"`, `priority = 0`.

### T-21 `[x]` Add `setrole` system call
- **depends**: T-20
- **files**: `xv6-src/kernel/syscall.c`, `xv6-src/kernel/syscall.h`,
  `xv6-src/kernel/sysproc.c`, `xv6-src/user/user.h`,
  `xv6-src/user/usys.pl`
- **verify**: a user program calls `setrole("parser")` and the change
  is reflected in `agentstat`.
- **estimate**: 1 h
- **assignee**: kernel

### T-22 `[x]` `agentstat` system call + userland program
- **depends**: T-21
- **files**: same as above + `xv6-src/user/agentstat.c`
- **verify**: running `agentstat` in the xv6 shell prints information
  for every active proc as one-line JSON.
- **estimate**: 2 h
- **assignee**: kernel

### T-23 `[x]` virtio serial channel (console-serial fallback path)
- **depends**: T-04
- **files**: `Makefile` (QEMU options), `host/proxy_pipe.py`
- **verify**: host-side `host/proxy_pipe.py` can receive the xv6
  guest's output and send input (echo test).
- **estimate**: 3 h
- **assignee**: agent
- **note**: the trickiest task. If virtio fails, fall back to
  `-chardev pipe`.

### T-24 `[x]` xv6-side proxy client user library
- **depends**: T-23
- **files**: `xv6-src/user/proxy_client.c`,
  `xv6-src/user/proxy_client.h`
- **verify**: from an xv6 user program, `proxy_call("echo", "hello")`
  returns the host's echo response.
- **estimate**: 2 h
- **assignee**: agent

### T-25 `[x]` Host proxy daemon — mock mode
- **depends**: T-23
- **files**: `host/proxy_daemon.py`
- **verify**: with `MODE=mock python host/proxy_daemon.py` running, an
  LLM request from xv6 receives a dummy response.
- **estimate**: 1 h
- **assignee**: agent

### T-26 `[x]` Host proxy daemon — live mode (Upstage integration)
- **depends**: T-25, T-04
- **files**: `host/proxy_daemon.py`
- **verify**: in `MODE=live`, a short LLM request from xv6 returns a
  solar-pro response.
- **estimate**: 1 h
- **assignee**: agent

### T-27 `[x]` LLM response cache (replay mode)
- **depends**: T-26
- **files**: `host/proxy_daemon.py`, `.cache/llm/`
- **verify**: the second call with identical input is served from
  cache (verify by timestamp comparison).
- **estimate**: 1 h
- **assignee**: agent

---

## 14. Phase 3 — Five Agents + IPC pipe (Late Week 11 ~ Early Week 12)

> Goal: five agent types operate inside xv6, connected by pipes.

### T-30 `[x]` Parser agent (xv6 userland program)
- **depends**: T-22, T-24
- **files**: `xv6-src/user/agent_parser.c`
- **verify**: receive log lines on stdin, call proxy, emit structured
  output.
- **estimate**: 2 h
- **assignee**: agent

### T-31 `[x]` Classifier agent
- **depends**: T-30
- **files**: `xv6-src/user/agent_classifier.c`
- **verify**: parser output → classification result
  (INFO/WARN/ERROR/CRITICAL).
- **estimate**: 1.5 h
- **assignee**: agent

### T-32 `[x]` Root-cause agent
- **depends**: T-31
- **files**: `xv6-src/user/agent_rootcause.c`
- **verify**: process only ERROR/CRITICAL inputs; pass everything else
  through.
- **estimate**: 1.5 h
- **assignee**: agent

### T-33 `[x]` Fix-suggester agent
- **depends**: T-32
- **files**: `xv6-src/user/agent_fixsuggest.c`
- **verify**: take root-cause output and produce a fix suggestion.
- **estimate**: 1.5 h
- **assignee**: agent

### T-34 `[x]` Evaluator agent
- **depends**: T-33
- **files**: `xv6-src/user/agent_evaluator.c`
- **verify**: verify each stage's output, then emit a pass/retry
  signal.
- **estimate**: 3 h
- **assignee**: agent

### T-35 `[x]` Orchestrator (`triage` command)
- **depends**: T-30, T-31, T-32, T-33, T-34
- **files**: `xv6-src/user/triage.c`
- **verify**: `triage samples/short.log` forks all 5 agents, wires
  pipes, and prints the result.
- **estimate**: 2 h
- **assignee**: agent

### T-36 `[x]` First end-to-end mock pass
- **depends**: T-35
- **files**: `tests/e2e_mock.sh`
- **verify**: `MODE=mock make e2e` passes.
- **estimate**: 1 h
- **assignee**: bench

---

## 15. Phase 4 — Evaluator Retry Loop (Mid Week 12)

### T-40 `[x]` Retry signal via `kill` + `sleep/wakeup`
- **depends**: T-34, T-35
- **files**: `xv6-src/kernel/proc.c` (extend sleep/wakeup if needed),
  `xv6-src/user/agent_evaluator.c`
- **verify**: when the Evaluator sends a retry signal to a worker,
  the worker re-executes.
- **estimate**: 4 h
- **assignee**: kernel + agent (collaboration)

### T-41 `[x]` Retry counter + 3-attempt cap
- **depends**: T-40
- **files**: `xv6-src/user/agent_evaluator.c`
- **verify**: with a deliberately-failing mock response, observe
  exactly 3 retries followed by a failure report.
- **estimate**: 1 h
- **assignee**: agent

---

## 16. Phase 5 — Scheduler Modification (Late Week 12) 🔴

> All tasks in this Phase are HUMAN GATE. They can break the xv6 boot
> itself.

### T-50 `[x]` 🔴 Create scheduler backup branch
- **depends**: T-36
- **files**: (human-performed) `git checkout -b backup/before-scheduler`
- **verify**: confirm the backup branch exists.
- **estimate**: 5 min
- **assignee**: human

### T-51 `[x]` 🔴 Add priority-queue structure to `sched.c`
- **depends**: T-50
- **files**: the `scheduler()` function in `xv6-src/kernel/proc.c`
- **verify**: `make autotest` passes (no regression) + priority
  application test passes.
- **estimate**: 4 h (1 h human review included)
- **assignee**: kernel (after human approval)

### T-52 `[x]` 🔴 Priority-measurement system call
- **depends**: T-51
- **files**: `xv6-src/kernel/sysproc.c`, `xv6-src/user/user.h`
- **verify**: running the same workload at different priorities yields
  measurable processing-time difference.
- **estimate**: 2 h
- **assignee**: kernel

---

## 17. Phase 6 — Evaluation (Week 13)

### T-60 `[x]` Complete `bench/run_all.sh`
- **depends**: T-36, T-41, T-52
- **files**: `bench/run_all.sh`
- **verify**: 5 experiments × 5 repetitions produce `out/bench/*.json`.
- **estimate**: 2 h
- **assignee**: bench

### T-61 `[x]` `bench/report.py` — auto-generate REPORT.md
- **depends**: T-60
- **files**: `bench/report.py`
- **verify**: `python bench/report.py out/bench/ > out/REPORT.md`
  produces a markdown report with tables + statistics.
- **estimate**: 2 h
- **assignee**: bench

### T-62 Real benchmark execution (human only)
- **depends**: T-61
- **files**: none
- **verify**: `bash bench/run_all.sh` runs end-to-end.
- **estimate**: 1 h (under human supervision)
- **assignee**: human

---

## 18. Phase 7 — Deliverables (Week 13~14)

### T-70 `[x]` Technical Report draft
- **depends**: T-62
- **files**: `docs/TECHNICAL_REPORT.md`
- **verify**: 5,000~8,000 words; §3.2 OS mapping table appears on the
  first page.
- **estimate**: 4 h (Opus recommended)
- **assignee**: human + claude

### T-71 Development Process Document
- **depends**: T-70
- **files**: `PROCESS.md`
- **verify**: weekly meeting minutes + decisions + issue resolutions
  included.
- **estimate**: 2 h
- **assignee**: human + claude

### T-72 English slides draft
- **depends**: T-70
- **files**: `slides/draft.md` (markdown → pptx)
- **verify**: 15-minute presentation length, English only.
- **estimate**: 3 h
- **assignee**: human + claude

### T-73 Demo GIF
- **depends**: T-36
- **files**: `docs/demo.gif`
- **verify**: a demo GIF of the `triage` command exists.
- **estimate**: 1 h
- **assignee**: human

---

## 19. Progress Summary

**Last updated**: 2026-05-26

**Completed tasks** (`[x]`):
- Phase 1: T-01~T-10 (10 items)
- Phase 2: T-20~T-27 (8 items)
- Phase 3: T-30~T-36 (7 items)
- Phase 4: T-40, T-41 (2 items)
- Phase 5: T-50~T-52 (3 items) 🔴
- Phase 6: T-60, T-61 (2 items)
- Phase 7: T-70 (1 item)

**Remaining tasks**:
| ID | Task | Assignee |
|---|---|---|
| T-62 | Real benchmark execution | human |
| T-71 | Development Process Document (`PROCESS.md`) | human + claude |
| T-72 | English slides draft | human + claude |
| T-73 | Demo GIF | human |

`docs/TECHNICAL_REPORT.md` Technical Report draft (5,007 words)
complete. The former standalone English translation
(`ImplementationPlan.en.md`) has also been absorbed into this English
master (`MASTER_PLAN.en.md`).

---

## 20. Rules for Adding Tasks

When adding a new task:
1. If it belongs to a new Phase, continue the Phase numbering
   (Phase 8, 9...).
2. If it belongs to an existing Phase, use the next available T-NN
   number.
3. State dependencies precisely (incorrect dependencies will wedge CC).
4. The `verify` command **must be automatically executable**. Things
   like "human visually checks" are not verifications.

---

## Appendix A — Implementation Methods Not Adopted

- **A.1 Python multiprocessing**: This calls OS features and fails the
  "directly implement" condition. Falls under the guideline's
  "thin-wrapper" warning.
- **A.2 Linux cgroups**: This uses Linux kernel features and is
  inconsistent with this project's direction of implementing resource
  management directly in xv6.
- **A.3 Sockets / gRPC**: Excessive complexity for a single-machine
  demo. xv6 internal IPC suffices.

---

## Appendix B — Self-Checklist (Guideline §3 README Requirements)

- [ ] One-paragraph project summary + chosen direction (A).
- [ ] Tech stack overview (xv6, QEMU, Python Proxy, Upstage Solar Pro 3).
- [ ] Setup instructions (QEMU install, xv6 build, `.env`, Upstage API key).
- [ ] Run instructions (`make qemu` → xv6 shell → `triage ...`).
- [ ] Demo screenshots / GIF.
- [ ] Repository public-visibility confirmed.
- [ ] Confirmed that the API key is not committed to git.
