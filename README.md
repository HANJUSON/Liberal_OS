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

직접 수정·신규 추가한 xv6 파일: `kernel/proc.{h,c}`, `kernel/console.c`, `kernel/syscall.{c,h}`, `kernel/sysproc.c`, `kernel/printf.c`, `user/usys.pl`, `user/user.h` + 신규 유저 프로그램(`triage`, `parser`, `classifier`, `rootcause`, `fixsuggest`, `evaluator`, `agentstat`, `setrole`, `priotest`, `procfields`, `proxytest`, `smoketest`, `logstress`).

## 3. OS 개념 매핑 (GuideLine §2 필수 제약)

| # | 컴포넌트 | OS 개념 | xv6 구현 |
|---|---|---|---|
| 1 | per-agent metadata | **프로세스 관리** | `struct proc`에 `agent_role[16]` / `priority` / `agent_state` 추가, `allocproc`/`kfork`/`freeproc` 패치 |
| 2 | 파이프라인 백본 | **IPC (pipe)** | 5-stage fork+pipe chain — `user/triage.c` |
| 3 | host 통신 프레이밍 | **IPC (line-framed)** | `PROXY_REQ\t<id>\t<role>\t<prompt>` / `PROXY_RES\t<id>\t<result>` |
| 4 | 동시 console·proxy 접근 | **동기화 (sleeplock)** | `cons_write_lock` + `proxy_lock`; 신규 시스콜 `proxylock(2)`/`proxyunlock(2)` |
| 5 | 에이전트 우선순위 | **스케줄링** | `scheduler()`에 max-priority 2-pass 선택 + `setprio(2)` 신규 시스콜 (nice-style [-20, 19] 클램프) |
| 6 | 장애 격리 | **프로세스 격리** | xv6 page-table 격리를 활용; `procfields` 테스트로 fork+pipe 의미 보존 검증 |
| 7 | 관측성 | **시스템 콜** | 신규 시스콜 6종: `setrole(22)`, `agentstat(23)`, `proxylock(24)`, `proxyunlock(25)`, `setprio(26)` + panic dump |

GuideLine §2가 요구하는 "최소 한 가지, 가능하면 그 이상"의 OS 개념을 **7개 직접 구현**. 설계 근거와 구현 디테일은 [`docs/TECHNICAL_REPORT.md`](docs/TECHNICAL_REPORT.md) §3·§5 참조.

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

```bash
pip install -r host/requirements.txt
# openai>=1.40.0, python-dotenv>=1.0.0
```

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
$ triage short.log     # 5-stage 파이프라인 실행
$ agentstat            # 활성 proc 메타데이터 JSON 출력
$ priotest             # 우선순위 스케줄러 효과 측정
# Ctrl-A X 로 QEMU 종료
```

### B. 호스트 자동 드라이브 (Proxy 경유 end-to-end)

```bash
# mock 모드 (네트워크 없음, autotest 기본):
python3 host/proxy_daemon.py --mode mock --triage short.log

# replay 모드 (캐시만 사용, 벤치마크 재현):
python3 host/proxy_daemon.py --mode replay --triage short.log

# live 모드 (실 Upstage API, 시연용):
python3 host/proxy_daemon.py --mode live --triage short.log
```

### C. 자동 테스트 · 회귀 · 벤치마크

```bash
make autotest                # 60s 헤드리스 xv6 부팅 + smoke 통과
make regression              # 커밋 전 회귀 게이트 (autotest + e2e mock)
bash bench/run_all.sh        # 5개 실험 5회 반복 (사람만 실행, Upstage 호출)
python3 bench/report.py out/bench/ > out/REPORT.md
```

## 6. 데모

xv6 셸에서 `triage short.log` 실행 시 5개 에이전트가 fork+pipe로 연결되어 로그를 처리하고, evaluator가 품질 검증 후 최대 3회 재시도 신호를 보낸다.

전체 시연 흐름의 **텍스트 트랜스크립트**는 [`docs/demo_transcript.txt`](docs/demo_transcript.txt)에 캡처되어 있다 (xv6 부팅 → `agentstat` 베이스라인 → `triage short.log` → `agentstat` → `priotest`). 재생성:

```bash
python3 bench/capture_demo.py > docs/demo_transcript.txt
```

`agentstat` 출력 형식 (`kernel/sysproc.c:160` 기준):

```json
[{"pid":1,"name":"init","role":"none","prio":0,"st":"sleep"},
 {"pid":2,"name":"sh","role":"none","prio":0,"st":"sleep"},
 {"pid":3,"name":"agentstat","role":"none","prio":0,"st":"run"}]
```

데모 GIF (`docs/demo.gif`)는 발표 전 사람이 화면 녹화로 추가 예정. 현재는 텍스트 트랜스크립트가 시연 증거로 사용 가능하다.

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
