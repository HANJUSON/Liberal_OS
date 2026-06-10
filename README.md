# Liberal_OS — xv6 기반 LLM 멀티에이전트 OS

> 2026 봄 OS 강좌 팀 프로젝트 · 방향 **A (OS for LLM)**

## 1. 한 문단 요약

**Liberal_OS**는 xv6 교육용 커널을 **직접 수정**하여 5종의 LLM 에이전트(parser / classifier / root-cause / fix-suggester / evaluator)를 xv6 프로세스로 실행하고, OS 수준의 **프로세스 격리·IPC pipe·동기화(sleeplock)·우선순위 스케줄링·신규 시스템 콜**로 오케스트레이션하는 시스템이다. 시연용 시나리오는 5단계 로그 트리아지 파이프라인이며, 각 에이전트는 호스트 측 Proxy Daemon을 통해 **Upstage Solar Pro 3** 모델에 `read`/`write`로 접근한다. 모든 OS 메커니즘은 Linux 라이브러리(`multiprocessing`/`cgroups`/스레드 풀)를 *호출*하지 않고 xv6 소스(`proc.h`, `proc.c`, `console.c`, `syscall.c`, `sysproc.c` 등)에서 *직접 설계·구현*했다 — GuideLine §2가 요구하는 "OS 컴포넌트는 직접 설계하고 구현한 것이어야 한다"는 제약을 정면으로 만족한다.

## 2. 기술 스택

| 계층 | 사용 기술 |
|---|---|
| 게스트 OS | **xv6-riscv** (직접 수정한 커널) |
| 가상화 | QEMU (riscv64) |
| LLM 백엔드 | **Upstage Solar Pro 3** (OpenAI 호환 API) |
| 호스트 측 중계 | Python 3.11+ (`openai`, `python-dotenv`), virtio console |
| 빌드 | GNU make, RISC-V 64-bit GCC 툴체인 (`riscv64-linux-gnu-gcc` 또는 `riscv64-unknown-elf-gcc` — Makefile이 자동 감지) |

직접 수정·신규 추가한 xv6 파일: `kernel/proc.{h,c}`, `kernel/console.c`, `kernel/syscall.{c,h}`, `kernel/sysproc.c`, `kernel/printf.c`, `kernel/verifier.{c,h}`, `kernel/cache.{c,h}`, `user/usys.pl`, `user/user.h` + 신규 유저 프로그램(`triage`, `parser`, `classifier`, `rootcause`, `fixsuggest`, `evaluator`, `agentstat`, `setrole`, `priotest`, `procfields`, `proxytest`, `smoketest`, `logstress`, `verifiertest`, `cachetest`).

## 3. OS 개념 매핑 (GuideLine §2 필수 제약)

| # | 컴포넌트 | OS 개념 | xv6 구현 |
|---|---|---|---|
| 1 | per-agent metadata | **프로세스 관리** | `struct proc`에 `agent_role[16]` / `priority` / `agent_state` 추가, `allocproc`/`kfork`/`freeproc` 패치 |
| 2 | 파이프라인 백본 | **IPC (pipe)** | 5-stage fork+pipe chain — `user/triage.c` |
| 3 | host 통신 프레이밍 | **IPC (line-framed)** | `PROXY_REQ\t<id>\t<role>\t<prompt>` / `PROXY_RES\t<id>\t<result>` |
| 4 | 동시 console·proxy 접근 | **동기화 (sleeplock)** | `cons_write_lock` + `proxy_lock`; 신규 시스콜 `proxylock(2)`/`proxyunlock(2)` |
| 5 | 에이전트 우선순위 | **스케줄링** | `scheduler()`에 max-priority 2-pass 선택 + `setprio(2)` 신규 시스콜 (nice-style [-20, 19] 클램프) |
| 6 | 장애 격리 | **프로세스 격리** | xv6 page-table 격리를 활용; `procfields` 테스트로 fork+pipe 의미 보존 검증 |
| 7 | 관측성 | **시스템 콜** | 신규 시스콜 5종(22-26): `setrole(22)`, `agentstat(23)`, `proxylock(24)`, `proxyunlock(25)`, `setprio(26)` + panic dump |
| 8 | verify+rollback closed loop (Pattern A) | **커널 검증·체크포인트** | `kernel/verifier.c` (정수 전용 순수함수 검증기 — LLM 제안 fix의 field/range/action-whitelist/protected-process 불변식 검사); 신규 시스콜 `verifyfix(27)`, `checkpoint(28)`, `restore(29)`. evaluator가 verify→FAIL:rollback(`restore`)+재시도 / PASS:`checkpoint`+accept |
| 9 | in-kernel semantic 응답 캐시 (Pattern B) | **커널 캐시 (의미 매칭·디스크 오버레이)** | `kernel/cache.c` — FNV-1a 64-bit exact + MinHash 시그니처/정수 Jaccard semantic(paraphrase) 매칭 + `/cache.bin` 디스크 오버레이; 신규 시스콜 `cacheget(30)`, `cacheset(31)`, `cacheclear(32)`. `user/proxy_client.h` `proxy_call()`에 결선 — 모든 에이전트가 PROXY_REQ 전 캐시 조회, hit 시 `CACHE_HIT` emit하고 LLM 호출 생략 |

위 5종(22-26)에 Pattern A/B의 신규 시스콜 6종(`verifyfix(27)`~`cacheclear(32)`)을 더해 **신규 시스콜 11종(22-32)**.

GuideLine §2가 요구하는 "최소 한 가지, 가능하면 그 이상"의 OS 개념을 **7개 핵심 + 2개 신규 서브시스템(Pattern A/B) 직접 구현**. 설계 근거와 구현 디테일은 [`docs/TECHNICAL_REPORT.md`](docs/TECHNICAL_REPORT.md) §3·§5 참조.

## 4. 셋업 안내

### 4.1 시스템 의존성 (Ubuntu / WSL2 기준)

```bash
# 기본 옵션 — Ubuntu/Debian 표준 패키지로 충분 (xv6-src/Makefile L36~50의 폴백이 자동 인식):
sudo apt-get install -y \
  gcc-riscv64-linux-gnu gdb-multiarch qemu-system-misc \
  make python3 python3-pip
```

대안: 별도 elf 툴체인이 이미 깔려 있는 환경(`riscv64-unknown-elf-gcc`, `riscv64-elf-gcc`, `riscv64-none-elf-gcc` 등)이라면 Makefile이 자동 감지해 우선 사용한다. Ubuntu/Debian 공식 apt 저장소에는 `gcc-riscv64-unknown-elf` 패키지가 없으므로 그 이름으로 `apt-get install`을 시도하면 실패한다 — 위 명령(`gcc-riscv64-linux-gnu`)을 사용한다.

QEMU 버전 5.1 이상 필요 (xv6-riscv 빌드 요구사항). `qemu-system-riscv64 --version`으로 확인.

### 4.2 Python 의존성

WSL2/최신 Ubuntu는 PEP 668(`externally-managed-environment`)이 켜져 있어 시스템 파이썬에 직접 설치하면 막힌다. **venv 권장**:

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r host/requirements.txt
# openai>=1.40.0, python-dotenv>=1.0.0
```

이후 모든 `host/*.py` 호출은 venv가 활성된 셸에서 수행. 시스템 파이썬에 강제로 설치하려면 `python3 -m pip install --user --break-system-packages -r host/requirements.txt`.

### 4.3 Upstage API 키 발급 및 등록

1. <https://console.upstage.ai/docs>에서 API 키 발급 (강사 배포 키 또는 개인 키).
2. 저장소 루트에 `.env` 작성 (**절대 커밋 금지** — `.gitignore`에 이미 포함):
   ```env
   UPSTAGE_API_KEY=up-xxxxxxxxxxxxxxxxxxxx
   MODE=mock          # mock | replay | live
   ```
3. 템플릿: `.env.example` 참조.

## 5. 실행 방법

### A. xv6 셸에서 직접 시연

```bash
make qemu
# xv6 셸 프롬프트 도달 후:
$ agentstat            # 활성 proc 메타데이터 JSON (베이스라인)
$ triage short.log     # 5-stage fork+pipe 파이프라인 (host daemon 없을 땐
                       # 첫 PROXY_REQ 프레임만 emit하고 응답 대기 hang — 정상)
$ priotest             # 우선순위 스케줄러 효과 (ρ → −1.0)
$ procfields           # fork 후 agent_role/priority/agent_state 보존 검증
$ setrole parser       # 현재 셸의 agent_role 설정 (신규 시스콜)
$ agentstat            # role 반영 확인
$ proxytest echo hello # 단일 PROXY_REQ/RES round-trip (host daemon 필요)
# Ctrl-A X 로 QEMU 종료
```

### B. 호스트 자동 드라이브 (Proxy 경유 end-to-end)

먼저 단일 호출로 Upstage 연결 검증:

```bash
MODE=live python3 host/hello_upstage.py
# 기대: {"ok":true,"mode":"live","reply":"...","elapsed_s":~1s}
# .env의 MODE 값은 CLI/환경변수가 덮어쓴다 (host/hello_upstage.py:51 _load_dotenv).
```

전체 파이프라인 — 토폴로지 두 가지:

```bash
# parallel (기본, fork+pipe 동시 실행, proxylock으로 직렬화):
python3 host/proxy_daemon.py --mode mock   --triage short.log              # 비용 0, deterministic
python3 host/proxy_daemon.py --mode replay --triage short.log              # 캐시 재생, instant
python3 host/proxy_daemon.py --mode live   --triage short.log --timeout 240  # 실 Upstage 호출

# sequential (shell ; 으로 stage 직렬, lock contention 0):
python3 host/proxy_daemon.py --triage-sequential short.log --mode live --timeout 240
```

live 모드 주의:
- 25 호출 × 0.3–1.5s ≈ 15–40초 wall-clock. 기본 `--timeout 20`은 부족 — **`--timeout 180`+** 권장.
- 호출당 trace는 stderr로. 시연용 깨끗한 출력은 `2>/dev/null`, 디버깅용은 `2> out/live-trace.log`.
- live 응답에 `\n`/`\t`가 섞이면 PROXY_RES line framing이 깨진다 → `host/proxy_daemon.py:live_handler`에서 `" ".join(raw.split())`로 sanitize. 시스템 프롬프트는 single-line 응답을 강제.
- 캐시 위치 `.cache/llm/<sha256>.json`. 한 번 live로 채우면 다음부턴 `--mode replay`로 1–2초 재현.

### C. 자동 테스트 · 회귀 · 벤치마크

```bash
make autotest                # 60s 헤드리스 xv6 부팅 + smoke 통과
make regression              # 커밋 전 회귀 게이트 6-stage (autotest + xv6 shell + proxy hello(mock)
                             #   + test_verifier + test_cache + capture_patterns 2-pattern 증거)
bash bench/run_all.sh        # 5개 실험 5회 반복 (사람만 실행, Upstage 호출 비용 발생)
python3 bench/report.py out/bench/ > out/REPORT.md
```

좀비 QEMU가 `fs.img`를 잡고 있으면 `qemu: Failed to get "write" lock` 발생 — 다음으로 정리:

```bash
ps -fu "$USER" | grep qemu-system-riscv64 | grep -v grep
# 보이는 PID에 kill (본인 소유 프로세스만)
```

## 6. 데모

xv6 셸에서 `triage short.log` 실행 시 5개 에이전트가 fork+pipe로 연결되어 로그를 처리하고, evaluator가 품질 검증 후 최대 3회 재시도 신호를 보낸다.

**스크린샷 인덱스** (모두 `out/screenshots/` 폴더):

| # | 파일 | 명령 | 무엇을 증명 |
|---|---|---|---|
| 1 | `screenshot1.png` | `MODE=live python3 host/hello_upstage.py` | Upstage Solar Pro 3 단일 호출 round-trip (`elapsed_s ≈ 1.07s`) |
| 2 | `screenshot2.png` | `python3 host/proxy_daemon.py --mode replay --triage short.log` | 5-stage × 5-line = **25 LLM 호출 완주**, `ok:true`, `missing_roles:[]` |
| 3 | `screenshot3.png` | `head -50 out/live-trace.log` | 25 호출의 per-call `CALL → DONE` trace — §2 "thin wrapper 아님" 증거 |
| 4 | `screenshot4.png` | `$ agentstat` (xv6 셸) | 신규 시스콜 `agentstat(23)` + `struct proc`에 추가한 메타 필드 |
| 5 | `screenshot5.png` | `$ priotest` (xv6 셸) | `setrole(2)`/`setprio(2)` + 수정 `scheduler()`가 우선순위 존중 (ρ = −1.000) |
| 6 | `screenshot6.png` | `$ triage short.log` (xv6 단독) | parser 자식이 raw PROXY_REQ 프레임 emit — fork+pipe 진입 증거 |
| 7 | `screenshot7.png` | `make regression` | 회귀 게이트 3/3 PASS |

증거 요약:
- **§6.1 Host-side**: Solar Pro 3 연결 및 25 호출 완주 (스크린샷 1·2·3)
- **§6.2 Guest-side**: xv6 OS 메커니즘 — proc 메타·스케줄러·IPC (스크린샷 4·5·6)
- **§6.3 회귀**: 자동 게이트 통과 (스크린샷 7)

---

### 6.1 Host-side 증거 — Upstage Solar Pro 3 연결

![Single live call to Upstage](out/screenshots/screenshot1.png)

*`MODE=live python3 host/hello_upstage.py` — Solar Pro 3에 단일 round-trip 호출 (`elapsed_s ≈ 1.07s`, mock의 0.05s 대비 ~20×).*

![Full 25-call pipeline](out/screenshots/screenshot2.png)

*xv6 안의 5-stage 에이전트가 fork+pipe로 연결된 채 호스트 daemon을 통해 Solar Pro 3에 **5 × 5 = 25회** round-trip. `ok:true`, `served:{parser:5,classifier:5,rootcause:5,fixsuggest:5,evaluator:5}`, `missing_roles:[]`, `eval_oks:5`. evaluator가 자연어로 실제 OS 조치(`top`, `htop`, `kill -15`, `journalctl --vacuum-time`)를 추천 — §3 표의 모든 OS 메커니즘이 실 LLM 호출과 함께 동작했다는 결정적 증거.*

![Per-call live trace](out/screenshots/screenshot3.png)

*`out/live-trace.log` (head 50줄): 각 `CALL → DONE` 쌍이 실제 Upstage 요청 1회. 25 호출이 호출당 0.3–1.4s에 차례로 통과 — GuideLine §2 "thin wrapper 아님"을 정면 반증.*

### 6.2 Guest-side 증거 — xv6 OS 메커니즘

![agentstat: proc metadata](out/screenshots/screenshot4.png)

*신규 시스콜 `agentstat(23)`이 `struct proc`에 추가한 메타 필드(`role`, `prio`, `st`)를 JSON 한 줄로 dump.*

![priotest: priority scheduling](out/screenshots/screenshot5.png)

*수정한 `scheduler()`가 `setprio(2)`로 설정한 priority를 존중: 6개 자식이 priority 5→0 순으로 종료 (ρ = −1.000).*

![triage standalone: fork+pipe entry](out/screenshots/screenshot6.png)

*xv6 단독 실행 — host daemon 없이도 `parser` 자식(pid 12)이 raw PROXY_REQ 프레임을 emit. fork+pipe 파이프라인의 첫 단계 진입 및 `proxylock(2)` 경로 통과 증거.*

### 6.3 회귀 게이트

![make regression PASS](out/screenshots/screenshot7.png)

*자동 회귀 스위트 (autotest + xv6 shell + proxy mock) 3/3 PASS.*

### 6.4 텍스트 트랜스크립트

전체 시연 흐름의 텍스트 트랜스크립트는 [`docs/demo_transcript.txt`](docs/demo_transcript.txt)에 캡처되어 있다 (xv6 부팅 → `agentstat` 베이스라인 → `triage short.log` → `agentstat` → `priotest`). 재생성:

```bash
python3 bench/capture_demo.py > docs/demo_transcript.txt
```

`agentstat` 출력 형식 (`kernel/sysproc.c:160` 기준):

```json
[{"pid":1,"name":"init","role":"none","prio":0,"st":"sleep"},
 {"pid":2,"name":"sh","role":"none","prio":0,"st":"sleep"},
 {"pid":3,"name":"agentstat","role":"none","prio":0,"st":"run"}]
```

## 7. 디렉토리 구조

```
liberal_os/
├── xv6-src/            # 수정한 xv6 소스
│   ├── kernel/         # proc, scheduler, console, syscall 등 수정
│   └── user/           # triage, parser, classifier, ... 유저 프로그램
├── host/               # 호스트 측 Proxy Daemon (Python)
├── tests/              # autotest.sh, regression.sh, e2e_mock.sh
├── bench/              # run_all.sh, report.py, summarize.py
├── samples/            # 입력 로그 (short.log)
├── docs/               # TECHNICAL_REPORT.md, demo 자료
├── slides/             # draft.md (영어 발표 자료)
├── out/                # 벤치/autotest 출력 (gitignore)
└── .env.example        # API 키 템플릿 (실 키는 .env, gitignore)
```

## 8. 최종 산출물 (GuideLine §5)

| # | 산출물 | 위치 |
|---|---|---|
| 1 | Application | 본 저장소 (`xv6-src/`, `host/`) + `make qemu` 시연 |
| 2 | Technical Report | [`docs/TECHNICAL_REPORT.md`](docs/TECHNICAL_REPORT.md) + [`out/REPORT.md`](out/REPORT.md) |
| 3 | Development Process Document | [`PROCESS.md`](PROCESS.md) |
| 4 | Presentation Slides (영어) | [`slides/draft.md`](slides/draft.md) |

## 9. 한계와 향후 작업

- **LLM API 직렬화**: `proxylock(2)`이 sibling 에이전트의 `proxy_call`을 직렬화하므로, 측정된 wall-clock 단축은 API 병렬화가 아니라 xv6 측 stage overlap에서 발생. request-id 멀티플렉싱 기반의 진정한 병렬 API 호출은 future work.
- **정적 DAG**: 5-stage 순서는 `triage.c`에 하드코딩. 동적 planner-executor·FS 기반 RAG·ReAct reflection 루프는 미구현.
- **파일시스템 활용 깊이**: xv6 기존 fs를 입력 로그 전달에만 사용. 에이전트 컨텍스트 저장/복원 메커니즘은 미구현.

설계 결정 기록과 작업 큐는 [`MASTER_PLAN.md`](MASTER_PLAN.md), 운영 매뉴얼은 [`CLAUDE.md`](CLAUDE.md), 회의록과 이슈 이력은 [`PROCESS.md`](PROCESS.md), 자율 하네스 설계는 [`HARNESS.md`](HARNESS.md) 참조.
