# Liberal_OS — Evaluation Report

Generated: 2026-06-10T02:30:05+00:00

Four measured experiments plus one analytical model. All runs
use **mock mode** (zero-latency LLM responses) so results are
deterministic and replayable without an Upstage API key.
Live-mode numbers require `STATUS.md` T-62 (human-authorised).

## §1. E2E mock pipeline (parallel xv6 procs)

| Metric | Mean | Stdev | N |
|---|---:|---:|---:|
| elapsed_s | 1.5142 | 0.041 | 5 |
| eval_fails | 0.0 | 0.0 | 5 |
| eval_oks | 4.8 | 0.4472 | 5 |
| eval_retries | 2.0 | 0.0 | 5 |
| evaluator_lines | 4.8 | 0.4472 | 5 |
| retry_injections | 3.4 | 1.3416 | 5 |

Each row aggregates one numeric field across captured runs of
`python3 host/proxy_daemon.py --mode mock --triage short.log`.

## §2. Evaluator retry effectiveness

| Metric | Value |
|---|---:|
| Lines evaluated (total) | 24 |
| Final pass (`evaluator:OK:`) | 24 |
| Final fail (`evaluator:FAIL:`, retries exhausted) | 0 |
| Retry signals issued | 10 |
| Final pass rate | 100.0% (24/24) |

Retry budget per line: **3** (hard cap in `agent_evaluator.c`).

**Quality caveat — mock determinism.** `mock_handler` is a pure
echo, so a line that fails first will fail again identically.
Under mock the retry **mechanism** is verified but the **quality
delta** is structurally 0 pp. Live mode (`MASTER_PLAN.md` T-62)
introduces LLM stochasticity where retries can change the
answer; the quality delta belongs in that report.

## §3. Priority scheduling effect (priotest)

| Metric | Value |
|---|---:|
| Children forked | 6 |
| Rho (priority vs finish order) | **-1.0** |
| Wall-clock | 0.061s |

Finish order vs assigned priority (higher = preferred):

| Order | Child idx | Priority | Tick offset |
|---:|---:|---:|---:|
| 0 | 0 | 5 | 0 |
| 1 | 1 | 4 | 0 |
| 2 | 2 | 3 | 0 |
| 3 | 3 | 2 | 0 |
| 4 | 4 | 1 | 1 |
| 5 | 5 | 0 | 1 |

Spearman ρ near −1.0 confirms our modified `scheduler()` honours
the `priority` field on `struct proc` (set via `setprio(2)`).
Tick offsets of 0 mean each child's CPU-bound loop completed
within a single timer tick — finish ORDER is the meaningful
signal here, not absolute time.

## §4. Sequential vs parallel — empirical (mock)

| topology | wall-clock mean (s) | stdev (s) | n |
|---|---|---|---|
| parallel (`triage`, fork+pipe)  | 1.5142 | 0.041 | 5 |
| sequential (`sh ;` + redirects) | 1.9848 | 0.0339 | 5 |

**Empirical speedup (mock)**: 1.31× (23.7% wall-clock savings).

Both rows execute the same 5-stage pipeline (parser → classifier →
rootcause → fixsuggest → evaluator) against the same `short.log`
input; the only variable is whether stages overlap (fork+pipe)
or fully serialise (shell `;` between file-redirected stages).

Caveat: mock-mode LLM latency `L ≈ 0`, so this ratio reflects
the xv6 plumbing component only — fork + pipe + scheduler
overlap vs serialised fork + wait + file I/O. It does **not**
measure LLM-call parallelism (that requires live mode — see §5).
The fact that even at L=0 the parallel topology saves ~24%
wall-clock validates that the fork+pipe chain itself yields real
overlap of stage work, independent of LLM call duration.

## §5. Sequential vs parallel — analytical projection (live)

Mock mode returns LLM responses instantly (`mock_handler` is
a pure echo), so empirically measured wall-clock reflects xv6
boot, fork/exec, and pipe-plumbing overhead — **not LLM-call
parallelism**. Under realistic latency `L` per LLM call:

```
  T_sequential  ≈  N_lines × N_stages × L
                =  5 × 5 × L  =  25·L

  T_parallel    ≈  (N_lines + N_stages − 1) × L      (steady-state pipeline)
                =  (5 + 5 − 1) × L  =  9·L

  speedup       ≈  25 / 9  ≈  2.78×
```

Observed parallel mean (mock): **1.5142s** — this is
the xv6-overhead floor with `L≈0`. Live-mode `L` of 200–800 ms
would shift the bottleneck onto LLM latency where the model
above predicts ≈2.78× speedup over a sequential variant.

**Caveat — proxy serialization.** `proxylock(2)`/`proxyunlock(2)`
(see TECHNICAL_REPORT §3 row 4) serialise the *send + recv* of
each `proxy_call` against sibling agents. Under live mode this
partially erodes the analytical speedup: stages overlap in
CPU/pipe work but their LLM transactions queue at the host
proxy. Future work — request-id multiplexing in proxy_daemon.
