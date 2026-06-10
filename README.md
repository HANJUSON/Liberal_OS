# Liberal_OS — an xv6-based multi-agent OS for LLMs

> Spring 2026 OS course team project · Direction **A (OS for LLM)**
> For detailed design, implementation, and evaluation see **[`docs/TECHNICAL_REPORT.md`](docs/TECHNICAL_REPORT.md)**; for the autonomous development harness see **[`HARNESS.md`](HARNESS.md)**.

---

## The one-line thesis

> **Not "call the API from Python `multiprocessing`" — the LLM agents are *processes* that a directly-modified xv6 kernel isolates, schedules, verifies, and caches.**

Five LLM agents (parser → classifier → root-cause → fix-suggester → evaluator) run as xv6 processes to triage logs, and every piece of orchestration is implemented **inside a directly-modified xv6 kernel** (`proc.c`, `console.c`, `syscall.c`, `verifier.c`, `cache.c`, …) — **not** in a Linux library such as `multiprocessing` / `cgroups`.

## Architecture

```
┌──────────────────────────── Linux host ────────────────────────────┐
│  host/proxy_daemon.py ──(OpenAI-compatible)──►  Upstage Solar Pro 3 │
│     mock = echo · replay = cache · live = real API                  │
└──────────▲───────────────│──────────────────────────────────────────┘
           │ PROXY_RES      │ PROXY_REQ        (QEMU -nographic console serial)
┌──────────│────────────────▼───────────── xv6 guest (QEMU riscv64) ───┐
│  triage (orchestrator) ── fork + pipe chain ──►                      │
│    parser → classifier → root-cause → fix-suggester → evaluator      │
│                                               │ proxy_call()         │
│  ════════ kernel (directly modified) ══════════╪═══════════════════  │
│   • proc metadata (agent_role/priority)    • priority scheduler      │
│   • sleeplock (console/proxy serialisation)                          │
│   • verifier.c ▸ Pattern A: verify→rollback      ── syscalls 27–29   │
│   • cache.c    ▸ Pattern B: FNV+MinHash+/cache.bin ── syscalls 30–32 │
│   • proc.c     ▸ per-process memory quota         ── syscall 33      │
└──────────────────────────────────────────────────────────────────────┘

proxy_call() flow:
  ① consult the kernel cache first — on a hit, skip the LLM call (PROXY_REQ) entirely (CACHE_HIT)
  ② on a miss, emit PROXY_REQ → receive the response → store it in the cache
  ③ the evaluator's fix proposal is checked by the kernel verifier — on FAIL, roll back and retry
```

## OS mechanisms implemented directly (summary)

The course guideline §2 requires OS components to be **directly designed and implemented**. Liberal_OS satisfies this with **7 core concepts + 3 new kernel subsystems**.

| Category | What |
|---|---|
| 7 core concepts | Process management (proc metadata) · IPC (pipes + line-framed protocol) · synchronisation (sleeplocks) · scheduling (priority) · system calls · process isolation |
| **Pattern A** | **verify+rollback closed loop** — `kernel/verifier.c` (an integer-only pure verifier) + syscalls `verifyfix(27)` / `checkpoint(28)` / `restore(29)`. The LLM is only a *proposer*; the kernel holds final authority. |
| **Pattern B** | **kernel semantic-cache short-circuit** — `kernel/cache.c` (FNV-1a exact + MinHash paraphrase matching + a `/cache.bin` disk overlay) + syscalls `cacheget(30)` / `cacheset(31)` / `cacheclear(32)`. On a hit the LLM call is skipped. |
| **Memory management / resource control** | **per-process memory quota** — `kernel/proc.{h,c}` · `sysproc.c` cap each agent process's resident heap. `growproc()` denies any `sbrk` past the cap (-1 + `AGENT_LOG`); syscall `setquota(33)` sets the cap (0 = unlimited) and `fork` inherits it. `agentstat` reports `rss_kb` / `quota_pg` / `qdenied`. |

**12 new syscalls (22–33)**, new kernel files `verifier.{c,h}` · `cache.{c,h}`, plus the per-process memory quota (`setquota(33)`).
👉 The full 10-row mapping table, design rationale, and measurements are in **[`docs/TECHNICAL_REPORT.md`](docs/TECHNICAL_REPORT.md) §3 · §10.8**.

## Quick start

```bash
# 0) build deps (Ubuntu/WSL2) + Python venv — details in TECHNICAL_REPORT Appendix B
sudo apt install -y gcc-riscv64-linux-gnu gdb-multiarch qemu-system-misc make python3 python3-venv
python3 -m venv .venv && . .venv/bin/activate && pip install -r host/requirements.txt

# 1) end-to-end (no network/key needed) — boot xv6 + run the 5-stage triage
python3 host/proxy_daemon.py --mode mock --triage short.log

# 2) regression gate (6 stages: boot · shell · proxy · Pattern A · Pattern B · two-pattern evidence)
make regression

# 3) drive the xv6 shell directly
make qemu        # at the shell: agentstat / priotest / triage short.log
```

Detailed run modes (live / replay / sequential, timeout & sanitise caveats) and setup troubleshooting: **TECHNICAL_REPORT Appendix B**.

## Demo — visual evidence

All screenshots live in `out/screenshots/`. (Captured 2026-06-08, a live demo *predating* Patterns A/B — evidence for the two patterns themselves is the text transcript [`docs/patterns_demo.txt`](docs/patterns_demo.txt).)

| # | File | Command | What it proves |
|---|---|---|---|
| 1 | `screenshot1.png` | `MODE=live python3 host/hello_upstage.py` | A single Upstage Solar Pro 3 round-trip (`elapsed_s ≈ 1.07s`) |
| 2 | `screenshot2.png` | `proxy_daemon.py --mode replay --triage short.log` | 5-stage × 5-line = **25 LLM calls completed**, `ok:true`, `missing_roles:[]` |
| 3 | `screenshot3.png` | `head -50 out/live-trace.log` | Per-call `CALL → DONE` trace for all 25 calls — evidence it is "not a thin wrapper" (§2) |
| 4 | `screenshot4.png` | `agentstat` (xv6 shell) | New syscall + the `struct proc` metadata fields |
| 5 | `screenshot5.png` | `priotest` (xv6 shell) | The modified `scheduler()` respects priority (ρ = −1.000) |
| 6 | `screenshot6.png` | `triage short.log` (xv6 standalone) | The parser child emits a raw PROXY_REQ — fork+pipe entry |
| 7 | `screenshot7.png` | `make regression` | Automated regression gate PASS (3-stage at capture time; now extended to **6-stage**) |

![Single live call to Upstage](out/screenshots/screenshot1.png)
*A single Solar Pro 3 round-trip (`≈1.07s`, ~20× the 0.05s mock).*

![Full 25-call pipeline](out/screenshots/screenshot2.png)
*The 5-stage agents, wired by fork+pipe, do **5 × 5 = 25** round-trips through the host daemon. `served:{parser:5,…,evaluator:5}`, `missing_roles:[]`, `eval_oks:5`. Every OS mechanism in the §3 table runs together with real LLM calls.*

![Per-call live trace](out/screenshots/screenshot3.png)
*`out/live-trace.log`: each `CALL → DONE` pair is one real Upstage request — a direct refutation of "thin wrapper".*

![agentstat: proc metadata](out/screenshots/screenshot4.png)
*`agentstat(23)` dumps the `struct proc` metadata fields (`role`, `prio`, `st`) as one JSON line.*

![priotest: priority scheduling](out/screenshots/screenshot5.png)
*The modified `scheduler()` respects the `setprio(2)` priority: 6 children exit in 5→0 order (ρ = −1.000).*

![triage standalone: fork+pipe entry](out/screenshots/screenshot6.png)
*Even without the host daemon, the `parser` child emits a raw PROXY_REQ frame — entry into the first fork+pipe stage.*

![make regression PASS](out/screenshots/screenshot7.png)
*Automated regression gate PASS (3/3 at capture time; now 6 stages including the Pattern A/B tests).*

Full-flow text transcript: [`docs/demo_transcript.txt`](docs/demo_transcript.txt) (boot → `agentstat` → `triage` → `priotest`); two-pattern e2e evidence: [`docs/patterns_demo.txt`](docs/patterns_demo.txt).

---

## Wrap-up — deliverables · layout · further reading

**Final deliverables (GuideLine §5)**

| # | Deliverable | Location |
|---|---|---|
| 1 | Application | This repository (`xv6-src/`, `host/`) + `make qemu` demo |
| 2 | Technical Report | [`docs/TECHNICAL_REPORT.md`](docs/TECHNICAL_REPORT.md) + [`out/REPORT.md`](out/REPORT.md) |
| 3 | Development Process Document | [`PROCESS.md`](PROCESS.md) |
| 4 | Presentation Slides (English) | [`slides/draft.md`](slides/draft.md) |

**Directory layout**

```
liberal_os/
├── xv6-src/   # modified xv6 (kernel: proc/console/syscall/verifier/cache · user: triage, 5 agents, …)
├── host/      # proxy_daemon.py (mock/replay/live) + hello_upstage.py
├── tests/     # autotest.sh · regression.sh · test_verifier.sh · test_cache.sh
├── bench/     # run_all.sh · report.py · capture_patterns.py
├── docs/      # TECHNICAL_REPORT.md · patterns_demo.txt · demo material
├── samples/   # input log (short.log)
└── out/       # bench/screenshot output (gitignored)
```

**Limitations (summary)** — LLM calls are serialised by `proxylock` (truly parallel API calls are future work); the 5-stage DAG is static. Full limitations and future work: [`docs/TECHNICAL_REPORT.md`](docs/TECHNICAL_REPORT.md) §11 · §12.

**Further reading** — design & schedule [`MASTER_PLAN.md`](MASTER_PLAN.md) · operating manual [`CLAUDE.md`](CLAUDE.md) · status ledger [`STATUS.md`](STATUS.md) · autonomous harness [`HARNESS.md`](HARNESS.md) · issue history [`PROCESS.md`](PROCESS.md).
