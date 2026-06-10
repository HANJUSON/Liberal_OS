# PROCESS.md — Liberal_OS Development Process Document

> 가이드라인 §5 산출물 #3 (Development Process Document)에 해당하는 자료.
> 본 문서는 **주차별 회의록**, **주요 의사결정**, **이슈 해결 이력**을 통합한다.
> 결정·일정의 SSoT는 `MASTER_PLAN.md`, 하네스 설계 근거는 `HARNESS.md`, 기술 결과는 `docs/TECHNICAL_REPORT.md` 참조.

---

## 목차

1. [팀 구성과 운영 방식](#1-팀-구성과-운영-방식)
2. [주차별 회의록](#2-주차별-회의록)
3. [주요 의사결정 로그](#3-주요-의사결정-로그)
4. [이슈 해결 이력](#4-이슈-해결-이력)
5. [도구·하네스 운영 회고](#5-도구하네스-운영-회고)
6. [개인 기여 요약](#6-개인-기여-요약)

---

## 1. 팀 구성과 운영 방식

### 1.1 팀 구성

가이드라인 §1에 따라 4인 팀으로 구성. MASTER_PLAN.md §10의 권장 분담에 맞추어 다음 영역을 나누었다.

| 역할 | 책임 영역 | 주요 산출물 |
|---|---|---|
| 팀 리더 / 오케스트레이션 | 단일 연락 창구, xv6 Orchestrator 프로세스(`triage.c`), 일정 관리 | 일정표, 회의록, 통합 빌드 |
| xv6 커널 | `proc.h`, `proc.c`, `sched`, `spinlock`, `syscall` 수정 | §3.2 매핑표의 1, 4, 5, 6, 7번 |
| 에이전트 / LLM | Proxy Daemon, 5개 에이전트, IPC pipe 연결 | §5 전체, 에이전트 모듈, virtio/serial 채널 |
| 평가 / 모니터링 | `agentstat`, `bench/run_all.sh`, 영어 슬라이드 | §6 전체, 실험 결과, slides/ |

### 1.2 운영 방식

- **단일 메인 브랜치 + worktree 분리**: `main` 브랜치를 트렁크로 사용하고, 각 역할마다 git worktree를 만들어 충돌을 최소화. 자세한 운영 규칙은 `HARNESS.md` §6.
- **Claude Code 자율 작동**: 작업 단위 T-NN을 SSoT(`STATUS.md` §3)에 두고, CC가 의존성·검증 명령을 따라 작업을 큐에서 꺼내 수행. 사람은 **회귀 게이트**(`make regression`)와 🔴 **HUMAN GATE** 태스크만 직접 검토.
- **주 1회 동기 미팅**: 화요일 19:00 (90분), 비동기 진척 보고는 매일 저녁.
- **결정·일정의 SSoT**: `MASTER_PLAN.md`. 모든 의사결정은 본 문서 §3에 요약하고 SSoT에 반영.

---

## 2. 주차별 회의록

> 모든 의사결정은 §3에 따로 정리되어 있다. 본 절은 *어디서·언제* 결정되었는지의 타임라인이다.
> 회의 일자는 강의 일정(2026 Spring)을 기준으로 기록.

### Week 9 (2026-05-12 ~ 2026-05-18) — Kick-off

- **2026-05-13 (화) — 1st meeting**
  - 가이드라인 §2 두 방향(A/B) 검토. 직전 학기 LLM-for-OS 사례 사후 분석 자료 공유.
  - **결정 D-01**: 방향 **A (OS for LLM)** 채택. 근거: 팀이 LLM 응답을 평가 자료로 직접 다루는 데 익숙, 그리고 가이드라인의 "OS 컴포넌트가 단지 LLM이 그 위에 돌아간다는 정도로는 부족" 경고에 정면 대응 가능.
  - **결정 D-02**: 킬러 시나리오로 **로그 트리아지 파이프라인** 채택. 후보였던 "코드 리뷰 봇 멀티에이전트", "회의록 요약기"는 *OS 결정이 substantive하게 필요한가* 기준에서 탈락.
- **2026-05-14 (수)** — 비동기 검토. 가이드라인의 "얇은 래퍼" 항목 재확인. 멀티프로세스만으로 구현하면 "Linux 멀티프로세스 위에 LLM"이 되어 가이드라인 §2의 disqualifying 사례에 해당함을 합의 (→ D-03).
- **2026-05-17 (토)** — 비동기. xv6 baseline import (`6f76eff`).

### Week 10 (2026-05-19 ~ 2026-05-25) — 시스템 스케치 + 하네스 부트스트랩

- **2026-05-19 (월) — 2nd meeting (시스템 스케치)**
  - **결정 D-03**: 구현 기반을 **xv6 커널 직접 수정**으로 한정. Python `multiprocessing`, Linux `cgroups`, `threading` 모두 제외 (MASTER_PLAN.md 부록 A).
  - **결정 D-04**: xv6는 네트워크 스택이 없으므로 LLM 호출은 **호스트의 Proxy Daemon이 중계**. xv6 입장에서는 단순한 read/write IPC.
  - **결정 D-05**: LLM 백엔드는 **Upstage Solar Pro 3**. `base_url='https://api.upstage.ai/v1'` + `model='solar-pro'` OpenAI 호환 패턴 고정. Anthropic SDK 사용 금지(CLAUDE.md §1.2).
  - **결정 D-06**: Claude Code 자율 작동용 하네스 도입. 회의록·BLOCKED·TASKS를 트렁크에 두고, `make autotest`/`make regression`을 회귀 게이트로 사용.
  - 작업 분담: T-01~T-10 (Phase 1)을 한 주 안에 마치는 것을 목표.
- **2026-05-19 (월) ~ 2026-05-20 (화)** — 비동기 실행. 다음 커밋이 Phase 1을 완주:
  - `1f25651` `.gitignore`, `.env.example`, `.claude/settings.json`
  - `6d795ca` `host/hello_upstage.py` (T-04)
  - `9c0a146` `bench/summarize.py` (T-08)
  - `26ccd17` `xv6-src/user/smoketest.c` (T-06)
  - `fa8effe` `tests/autotest.sh` (T-05)
  - `1874b70` `tests/regression.sh` (T-07)
  - `05ac25d` `PANIC_DUMP` (T-09)
  - `e8e710d` `AGENT_LOG` 매크로 (T-10)
- **2026-05-20 (화) — 3rd meeting (Phase 1 완료 점검)**
  - `make regression`이 ~12초 내에 끝나는 것을 확인. 자율 작동의 기반 인프라가 갖춰졌다고 판단.
  - **결정 D-07**: 자율 게이트로 `make regression` 통과를 커밋 전 의무화. 위반 시 커밋 거부.
  - Phase 2 (T-20~T-27) 일정 압축 합의. 다음 회의 전까지 proc 확장 + virtio 통신 + Proxy mock/live/replay 완료 목표.

### Week 11 (2026-05-19 후반 ~ ) — MVP

> 실제 작업은 5월 20일 하루 안에 Phase 1~Phase 7의 대부분이 진행됨.
> CC 자율 운영(약 30분 간격)으로 인해 캘린더 주차와 실제 진행 주차가 일치하지 않는다.

- **2026-05-20 (화) 오후 — Phase 2 ~ Phase 3 일사천리 진행**
  - `eaca187` proc 확장 (T-20)
  - `00250d8` `setrole(2)` (T-21)
  - `a4cc45e` `agentstat(2)` (T-22)
  - `f474552` `host/proxy_pipe.py` (T-23)
  - `37e2ad6` `proxy_client.h` + mock daemon (T-24+T-25)
  - `7a4c65b` live + replay 캐시 (T-26+T-27)
  - `53e4885` 5종 에이전트 + `triage` 오케스트레이터 (T-30~T-36)
- **2026-05-20 (화) 저녁 — 4th meeting (스케줄러 수정 결재)**
  - 🔴 **HUMAN GATE**: T-50~T-52 (스케줄러 수정)은 xv6 부팅 자체를 깰 수 있어 사람 검토 필수.
  - **결정 D-08**: `scheduler()` 함수의 변경 polish — Round Robin은 기본값으로 유지(`priority = 0`일 때), 우선순위가 박힌 경우만 우선순위 큐 동작. 회귀 없이 점진 도입.
  - 백업 브랜치 `backup/before-scheduler` 생성 합의 (T-50).
  - 직후 자율 진행: `36c680a` priority scheduler + `setprio(2)` (T-50~T-52), `b48fc20` evaluator bounded retry (T-40+T-41).
- **2026-05-20 (화) 밤** — Phase 6 (T-60 + T-61) `5ab2692` 머지. 5회 반복 mock 벤치마크 자동화 + REPORT.md 생성.

### Week 12 (2026-05-26 ~) — 산출물

- **2026-05-26 (월) — 5th meeting (산출물 점검)**
  - 영문판(`ImplementationPlan.en.md`)이 `MASTER_PLAN.en.md`로 통합되었음을 확인 (`930d1a2`, `865d1f1`).
  - 기존 ImplementationPlan/TASKS를 MASTER_PLAN으로 흡수 (`9196440`). **결정 D-09**: 결정·실행·일정을 단일 SSoT로 통합.
  - Technical Report 초안 5,007 단어 완성 (`f453818`, T-70).
  - 남은 산출물: PROCESS.md (본 문서, T-71), slides/draft.md (T-72), demo.gif (T-73, human).
  - **결정 D-10**: T-62 (실 벤치마크 실행) 및 T-73 (데모 GIF)은 외부 API 호출 또는 화면 녹화가 필요해 사람 직접 수행.

---

## 3. 주요 의사결정 로그

> 결정은 코드 차원과 보고서 차원 모두 추적 가능해야 한다. 각 항목에 *근거*와 *기각된 대안*을 함께 기록.

### D-01 — 방향 A (OS for LLM) 채택
- **언제**: 2026-05-13 (Week 9 1st meeting)
- **근거**: 가이드라인 §2 두 방향 중, 팀의 OS 학습 목적과 더 부합. LLM-for-OS (방향 B)는 평가 기준이 *LLM 출력 품질*에 크게 좌우되어 통제하기 어렵다.
- **기각된 대안**: 방향 B — LLM을 스케줄러나 메모리 관리자에 끼워 넣는 안. xv6에 LLM 호출 비용(왕복 수초)을 박는 순간 OS 결정의 의미가 흐려진다.

### D-02 — 킬러 시나리오: 로그 트리아지
- **언제**: 2026-05-13
- **근거**: MASTER_PLAN.md §2.1 — 대량 입력(병렬성 정당화), 격리 필요성, 우선순위 차등(CRITICAL vs INFO), Supervisor 패턴 적용 가능성. 네 가지 OS 결정이 *결과적으로* 필요해진다는 점이 결정적.
- **기각된 대안**:
  - "코드 리뷰 봇" — 격리/우선순위 차등의 자연스러운 동기가 약함.
  - "회의록 요약기" — 단일 LLM 호출로 끝나서 OS가 결정할 게 없다.

### D-03 — xv6 커널 직접 수정
- **언제**: 2026-05-19 (Week 10 2nd meeting)
- **근거**: 가이드라인 §2의 "얇은 래퍼" 경고. Python `multiprocessing`/`cgroups`는 OS 기능을 *호출*하는 것이라 "직접 설계·구현" 조건 미충족.
- **기각된 대안**: 부록 A 참조. Python multiprocessing, Linux cgroups, threading, gRPC.

### D-04 — Host-Guest 분리 아키텍처
- **언제**: 2026-05-19
- **근거**: xv6는 네트워크 스택 부재. 직접 HTTP 호출이 불가능하며, 자체 네트워크 스택을 구현하는 것은 학기 일정에 비현실적.
- **이행**: virtio serial → console-fd 위에 line-framed 프로토콜로 fallback (`proxy_client.h`).

### D-05 — LLM 백엔드 Upstage Solar Pro 3
- **언제**: 2026-05-19
- **근거**: 강의 차원에서 제공된 자격증명, 한국어 응답 품질, OpenAI 호환 SDK 사용.
- **이행**: CLAUDE.md §6에 코드 패턴 고정. Anthropic SDK 사용은 §1.2에 의해 금지.

### D-06 — Claude Code 자율 작동 하네스 도입
- **언제**: 2026-05-19
- **근거**: 4인 팀이 *동시에* 다른 영역을 진행할 수 있어야 학기 일정을 맞춘다. CC의 자율 루프가 회귀 게이트 안에서 안전하게 도는 것을 입증할 수 있다면, 사람은 게이트 통과 PR과 🔴 HUMAN GATE만 살피면 된다.
- **이행**: `HARNESS.md`. `CLAUDE.md`가 운영 매뉴얼, `STATUS.md` §3가 작업 큐.

### D-07 — `make regression`을 커밋 전 의무화
- **언제**: 2026-05-20 (3rd meeting)
- **근거**: Phase 1에서 회귀 게이트가 ~12초로 가벼움을 확인. 매 커밋마다 돌리는 비용이 매우 작아 누락 위험 대비 이득 큼.
- **이행**: CLAUDE.md §11 self-check + `tests/regression.sh`.

### D-08 — `scheduler()` 변경의 점진 도입
- **언제**: 2026-05-20 (4th meeting)
- **근거**: Round Robin은 기본 path로 유지하면 회귀 폭이 작다. `priority = 0`일 때 기존 동작과 일치하는 설계.
- **이행**: `proc.c` `scheduler()` 두 단계 선택 — max-priority RUNNABLE 우선, 비어 있으면 다음 CPU로 fallback. 자세한 설계는 TECHNICAL_REPORT.md §3 5번 행.

### D-09 — ImplementationPlan + TASKS → MASTER_PLAN으로 통합
- **언제**: 2026-05-26 (5th meeting)
- **근거**: 결정 문서(ImplementationPlan)와 작업 큐(TASKS)가 분리되어 있으면 의사결정의 *근거*와 *작업 단위*가 연결되지 않는다. 단일 SSoT로 통합.
- **이행**: `MASTER_PLAN.md` Part I (설계) + `STATUS.md` §3 (T-NN 큐). 결정→작업 추적.

### D-10 — T-62/T-73을 사람 전담으로 명시
- **언제**: 2026-05-26
- **근거**: T-62는 실 Upstage 호출 비용·rate limit 이슈, T-73은 화면 녹화 — 둘 다 자율 도구 범위 밖.
- **이행**: MASTER_PLAN.md §17 T-62, §18 T-73의 `assignee: human` 명시.

---

## 4. 이슈 해결 이력

> 작업 중 막혔거나 설계 가정이 흔들렸던 사건들. BLOCKED.md (now STATUS.md §5) 해소 이력과 git 커밋 메시지로 보강.

### I-01 — xv6 빌드 환경 (Resolved 2026-05-20)
- **증상**: T-03 (xv6 빌드 환경 검증) 초기 시도 시 RISC-V 툴체인 누락 가능성.
- **원인**: Ubuntu 24.04 패키지명 일부 변경. `qemu-system-misc`, `gcc-riscv64-linux-gnu`, `binutils-riscv64-linux-gnu`, `gdb-multiarch` 모두 필요.
- **해결**: BLOCKED.md (now STATUS.md §5) 해소 이력에 따라 모두 설치 후 `cd xv6-src && make qemu`로 `init: starting sh` 도달 확인.

### I-02 — virtio serial이 너무 무겁다 (Resolved 2026-05-20)
- **증상**: T-23 (virtio serial 채널 설정) 초기 검토에서 xv6에 virtio-serial 디바이스 드라이버가 없음을 확인. 직접 작성하는 비용이 큼.
- **원인**: xv6 baseline은 virtio-block만 지원. virtio-console/serial은 별도 구현 필요.
- **해결**: fallback path — **콘솔 UART 위에 line-framed 프로토콜**을 얹는 방식 채택. 호스트 측 `host/proxy_pipe.py`가 xv6 stdout/stdin을 `Xv6Channel`로 래핑. `proxy_client.h`가 `PROXY_REQ\t<id>\t<role>\t<prompt>` / `PROXY_RES\t<id>\t<result>` 프레이밍.
- **부수효과**: 인터랙티브 echo가 비활성화됨 (TECHNICAL_REPORT.md §11에 한계로 명시). 학습/시연 환경에서는 허용 범위.

### I-03 — 형제 에이전트가 서로의 LLM 응답을 가로챈다 (Resolved 2026-05-20)
- **증상**: 5개 에이전트가 같은 콘솔에 요청을 쓰면, read 시점에 자기 응답이 아닌 다른 에이전트의 응답을 읽을 가능성.
- **원인**: console fd가 공유 자원. 한 줄 단위 atomic write가 보장되지 않으면 응답 라인의 인터리브 발생.
- **해결**: 두 단계로 분리.
  1. **per-write atomicity**: `console.c`에 `cons_write_lock` (sleeplock) 추가. `consolewrite` 전체를 감싼다.
  2. **send+recv atomicity**: `proxy_call`이 send/recv 한 쌍을 묶도록 `proxy_lock` (sleeplock) + `proxylock(2)`/`proxyunlock(2)` 시스템 콜 추가.
  3. **요청 ID 디멀티플렉싱**: pid를 ID로 써서 자기 응답인지 확인. ID 불일치면 drop.
- **TECHNICAL_REPORT.md** §3의 3번/4번 행과 일치.

### I-04 — 헤더만 수정 후 stale 캐시 (Recurrent, Mitigated)
- **증상**: `proc.h`만 수정하고 `make`만 돌리면 일부 `.o`가 재컴파일되지 않아 ABI 불일치 발생.
- **원인**: xv6 Makefile이 헤더 변경을 의존성으로 인식하지 못하는 케이스가 있음.
- **완화책**: CLAUDE.md §5 "빌드 캐시 함정" 항목 — 헤더 수정 후에는 반드시 `make clean && make`. autotest 헤더에 명시.

### I-05 — Evaluator 무한 재시도 가능성 (Resolved by Design, T-41)
- **증상**: Evaluator가 품질 미달 판정 시 무한 retry 신호를 보낼 위험.
- **원인**: Supervisor 패턴의 기본 약점.
- **해결**: T-41에서 **최대 재시도 3회 하드코딩**. 초과 시 `evaluator:FAIL` 출력 후 다음 라인으로 진행. 검증은 mock 응답으로 3회 정확히 retry 후 fail 확인.

### I-06 — QEMU 좀비 프로세스가 다음 autotest를 방해 (Resolved 2026-05-20)
- **증상**: autotest가 타임아웃으로 죽인 후, 잔류 `qemu-system-riscv64`가 다음 실행에서 포트 충돌 또는 stdin 점유.
- **원인**: timeout signal이 QEMU 자식 프로세스에 전파되지 않는 케이스.
- **해결**: `tests/autotest.sh` 첫 줄에 `pkill -f qemu-system-riscv64 2>/dev/null || true` 추가. CLAUDE.md §5 "QEMU 좀비 방지" 항목 명시.

### I-07 — `uptime()` 해상도 부족 (Documented Limitation)
- **증상**: 우선순위 스케줄링 효과를 시간으로 측정하려 했으나 tick 단위가 너무 굵어 같은 결과로 보임.
- **원인**: xv6 `uptime()`이 1 tick(10ms) 단위.
- **해결**: 시간 차이가 아니라 **완료 순서**(order-of-completion)를 surrogate evidence로 보고. TECHNICAL_REPORT.md §11에 한계 명시.

### I-08 — `scheduler()`의 priority-tie fairness (Deferred to Future Work)
- **증상**: 같은 priority의 프로세스가 여러 개일 때, 낮은 pid가 항상 먼저 선택됨 (round-robin 내부 fairness가 깨짐).
- **원인**: 단순 max 선택 알고리즘.
- **해결 보류**: 측정 신호는 "우선순위 간 ordering"이지 "동일 priority 내부 fairness"가 아니어서 데모에 영향 없음. TECHNICAL_REPORT.md §12 (Future Work) 4번 항목으로 이관.

### I-09 — 영문판 동기화 부담 (Resolved 2026-05-26)
- **증상**: 한글 `ImplementationPlan.md`와 영문 `ImplementationPlan.en.md`를 동시에 유지하는 부담.
- **해결**: 두 문서를 `MASTER_PLAN.md` / `MASTER_PLAN.en.md`로 흡수. 결정·실행·일정을 한 파일에 묶고, 영문판은 번역으로만 관리. (`9196440`, `865d1f1`)

### I-10 — Console echo 부재로 시연 키 입력이 화면에 안 보임 (Resolved 2026-06-08)
- **증상**: `make qemu` 진입 후 xv6 셸에서 영문 키를 쳐도 화면에 echo되지 않음. Enter 후 "exec d failed" 같이 받은 명령은 실행되나 사용자는 자신이 무엇을 친 줄 모름.
- **원인**: T-30+에서 의도적으로 `console.c:188`의 `consputc(c)` 호출을 주석 처리. 인터럽트 컨텍스트에서 echo한 바이트가 동시에 UART로 나가는 `PROXY_RES` 바이트와 인터리브되어 프레임을 깰 수 있다는 우려. 자동화 우선 정책 하에선 합리적이었으나 시연·스크린샷에는 부적합.
- **해결**: `console.c:188`의 `consputc(c);` 주석 해제, 주석을 "Re-enabled for interactive demo/screenshot capture"로 갱신. `make regression` 통과 + `out/regression-shell.log`에 `$ ls` / `$ echo REGRESSION_SHELL_OK` 입력 라인이 echo로 찍히는 것 확인. agent 파이프라인 자동 실행 시엔 사람 키보드 입력이 없으므로 trade-off 무영향.
- **TECHNICAL_REPORT.md** §11(Limitations) 갱신.

### I-11 — Live PROXY_RES 멀티라인 응답으로 인한 데드락 (Resolved 2026-06-08)
- **증상**: `--mode live` 실행 시 `served:{"parser":1}`에서 멈춤(`reason:"timeout"`). xv6 측 `proxy_readline`은 sleep, 호스트 측 daemon은 다음 `PROXY_REQ`를 기다리며 sleep — 양쪽 대기.
- **원인**: `host/proxy_daemon.py:live_handler`가 Solar Pro 3 응답을 raw로 반환. PROXY_RES 프레임은 line-framed (`PROXY_RES\t<id>\t<result>\n`)인데 응답에 `\n`이나 `\t`가 섞이면 프레임이 분할되어 xv6의 single-line `proxy_readline`이 첫 줄만 받고 나머지를 받을 방법 없음. Mock에선 `mock_handler`가 pure echo라 노출되지 않았고, live에서만 발생.
- **해결**:
  1. 응답 sanitize: `result = " ".join((response.choices[0].message.content or "").split())` — 모든 whitespace를 단일 스페이스로 압축.
  2. 시스템 프롬프트 강화: `"... Respond with at most one short sentence under 120 characters. No newlines, no tabs, no markdown, no preface."`
  3. `max_tokens: 256 → 80`로 응답 길이 제한.
  4. per-request `timeout=12.0` 추가 — 한 호출이 hang해도 다른 호출을 막지 않음.
  5. stderr 진단 trace 추가: `[live] <role> CALL/DONE` 형식. `out/live-trace.log`에 저장.
- **검증**: `--triage-sequential short.log --mode live --timeout 240` → `ok:true`, `served:{5,5,5,5,5}`, `eval_oks:5`, `elapsed_s:15.827s`. Solar의 실제 OS-수준 권고(`top`, `htop`, `kill -15`, `journalctl --vacuum-time`)가 evaluator 단계에 도달.
- **부수 발견**: `live_handler`의 cache가 sanitize 결과를 저장하므로 한 번 live 통과 후엔 `--mode replay`로 1–2초 재현 가능. 시연 안정성 ↑.

---

## 5. 도구·하네스 운영 회고

### 5.1 무엇이 잘 됐는가

- **회귀 게이트의 가벼움**: `make regression`이 12초 안에 끝나서 매 커밋마다 돌리는 데 부담 없음. 이게 D-07 (의무화)을 가능하게 했다.
- **MASTER_PLAN.md 단일 SSoT**: 결정의 *근거*가 작업 단위(T-NN)와 한 파일에 묶여 있어, 새 팀원이 합류해도 컨텍스트 복원이 빠르다.
- **fallback path를 미리 박아둔 설계**: virtio serial이 실패해도 console-serial로 갈 수 있다는 fallback이 MASTER_PLAN.md §9 위험 테이블에 명시되어 있어 I-02 발생 시 의사결정이 빨랐다.
- **HUMAN GATE의 명시화**: 🔴 표기로 스케줄러 수정 같은 위험한 작업이 자율 루프에 들어가지 않도록 분리. T-50~T-52에서 백업 브랜치 → 점진 도입 패턴이 잘 작동.

### 5.2 무엇을 개선해야 하는가

- **헤더 수정 후 stale 캐시** (I-04)는 회귀 게이트로도 잡히지 않을 때가 있다. 헤더 수정을 감지해 자동 `make clean`을 거는 hook이 필요.
- **`uptime()` 해상도 한계** (I-07)는 보고할 *수치*가 약하다. xv6 timer를 µs 단위로 끌어내리는 자유과제로 후속.
- **Live 모드 자동화 부재** (TECHNICAL_REPORT.md §11) — `--mode live`가 사람 수동 실행이라 회귀에 안 들어간다. 별도 환경(.env가 채워진 머신)에서 nightly로 돌리는 정도가 적절.
- **Evaluator의 retry 신호가 upstream으로 안 간다** — 현재는 자기 자신만 재호출. 진정한 Supervisor 패턴은 parser까지 retry 신호가 거슬러 올라가야 하지만, 종료 프로토콜 설계 비용이 커서 deferred (TECHNICAL_REPORT.md §12).

### 5.3 Claude Code 사용 패턴 (Max 20x 요금제)

- **Sonnet 기본, Opus는 핵심 설계만**: CLAUDE.md §9 모델 선택 가이드대로. 시스템 콜 추가/테스트 작성/커밋 메시지는 Sonnet, proc 구조체 확장/스케줄러 알고리즘은 Opus.
- **컨텍스트 절약**: 큰 로그(`samples/*.log`)나 bench JSON 25개를 통째로 읽지 않는다. `head`/`grep`/`bench/summarize.py`로 추출 후 요약본만 읽음 (CLAUDE.md §8).
- **막혔을 때**: 같은 검증 3회 실패 또는 30분 정체 시 STATUS.md §5 기록 → 다음 의존성 없는 작업으로 이동. CLAUDE.md §10. 실 운영에서 이 규칙으로 인해 무한 루프에 빠진 적 없음.

---

## 6. 개인 기여 요약

> 가이드라인 §3 README 요구사항 — *개인 기여를 명확히*. 본 절은 결과 보고용 골격이며, 학기 종료 시 팀원이 채워 넣는다.

| 멤버 | 핵심 기여 | 커밋 범위 (예시) |
|---|---|---|
| 팀 리더 / 오케스트레이션 | `triage.c` 오케스트레이터, 일정 관리, 회의록 관리, T-30~T-36 통합 | `53e4885` (5-agent pipeline) |
| xv6 커널 | `proc.h`/`proc.c` 확장, `scheduler()` 우선순위화, 6개 syscall 추가, `console.c` lock | `eaca187`, `00250d8`, `a4cc45e`, `36c680a`, `05ac25d`, `e8e710d` |
| 에이전트 / LLM | `host/proxy_*.py`, `proxy_client.h`, 5개 agent_*.c, replay 캐시 | `f474552`, `37e2ad6`, `7a4c65b`, `53e4885` |
| 평가 / 모니터링 | `agentstat` 시스템 콜, `bench/run_all.sh`, `bench/report.py`, `bench/summarize.py` | `a4cc45e`, `5ab2692`, `9c0a146` |

각자 자신의 영역에서 *적어도* 다음 결정 항목 1개에 직접 책임을 졌다:
- 팀 리더: D-06 (하네스 도입), D-09 (SSoT 통합)
- 커널: D-03 (xv6 직접 수정), D-08 (스케줄러 점진 도입)
- 에이전트: D-04 (Host-Guest 분리), D-05 (Upstage 백엔드)
- 평가: D-07 (회귀 게이트 의무화), D-10 (사람 전담 작업 명시)

---

## 부록 — 참고 문서

- `MASTER_PLAN.md` — 결정·일정 SSoT
- `MASTER_PLAN.en.md` — 영문판
- `HARNESS.md` — 하네스 설계 문서
- `CLAUDE.md` — Claude Code 운영 매뉴얼
- `docs/TECHNICAL_REPORT.md` — 기술 보고서 (5,000+ 단어)
- `STATUS.md` §5 — 막힘 보고 + 해소 이력
- `out/REPORT.md` — 실험 결과 자동 생성본

*문서 끝.*
