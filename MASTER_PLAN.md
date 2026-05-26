# 마스터 플랜 — Liberal_OS (xv6 기반 LLM 멀티에이전트 OS)

> 본 문서는 프로젝트의 **설계 결정**과 **작업 일정**을 통합한 단일 출처(SSoT)다.
> 기존 `ImplementationPlan.md`(결정 문서)와 `TASKS.md`(작업 큐)를 흡수했다.
> GuideLine §5의 산출물 #2(Technical Report)와 #3(Development Process Document)의 통합 초안 역할을 겸한다.

---

## 목차

**Part I — 계획 (설계 결정)**
1. [프로젝트 개요](#1-프로젝트-개요)
2. [킬러 시나리오 — 로그 트리아지 파이프라인](#2-킬러-시나리오--로그-트리아지-파이프라인)
3. [시스템 아키텍처](#3-시스템-아키텍처)
4. [구현 방법 결정](#4-구현-방법-결정)
5. [LLM 백엔드 — Upstage Solar Pro 3](#5-llm-백엔드--upstage-solar-pro-3)
6. [평가 계획](#6-평가-계획)
7. [Week 10~14 일정 개괄](#7-week-1014-일정-개괄)
8. [산출물 매핑 (가이드 §5)](#8-산출물-매핑-가이드-5)
9. [위험과 대응](#9-위험과-대응)
10. [역할 분담](#10-역할-분담)

**Part II — 작업 일정 (T-NN 작업 큐)**
11. [작업 형식과 상태 표기](#11-작업-형식과-상태-표기)
12. [Phase 1 — 하네스 부트스트랩](#12-phase-1--하네스-부트스트랩-week-10)
13. [Phase 2 — xv6 proc 확장 + virtio](#13-phase-2--xv6-proc-확장--virtio-week-11-전반)
14. [Phase 3 — 에이전트 5종 + IPC pipe](#14-phase-3--에이전트-5종--ipc-pipe-week-11-후반--week-12-전반)
15. [Phase 4 — Evaluator 재시도 루프](#15-phase-4--evaluator-재시도-루프-week-12-중반)
16. [Phase 5 — 스케줄러 수정](#16-phase-5--스케줄러-수정-week-12-후반-🔴)
17. [Phase 6 — 평가](#17-phase-6--평가-week-13)
18. [Phase 7 — 산출물](#18-phase-7--산출물-week-1314)
19. [진행 현황 요약](#19-진행-현황-요약)
20. [작업 추가 규칙](#20-작업-추가-규칙)

**부록**
- [부록 A — 채택하지 않은 구현 방법](#부록-a--채택하지-않은-구현-방법)
- [부록 B — 셀프 체크리스트 (가이드 §3 README 요구사항)](#부록-b--셀프-체크리스트-가이드-3-readme-요구사항)

---

# Part I — 계획 (설계 결정)

## 1. 프로젝트 개요

### 1.1 한 문단 요약 (가이드 §3 요구)

**Liberal_OS**팀은 xv6 커널을 직접 수정하여 여러 LLM 에이전트를 **OS 레벨의 격리·스케줄링·자원 관리 메커니즘**으로 오케스트레이션하는 시스템을 구현한다. 각 에이전트(parser / classifier / root-cause / fix-suggester / evaluator)는 xv6 프로세스로 표현되며, 오케스트레이터는 직접 구현한 IPC(pipe)로 에이전트 간 통신을 수행하고, 직접 수정한 스케줄러로 에이전트별 우선순위를 부여한다. Evaluator 에이전트는 Worker 에이전트의 출력을 검증하고 품질 미달 시 재시도 신호를 보내는 Supervisor 패턴을 구현한다. 실제 LLM API 호출은 QEMU 호스트의 Proxy Daemon이 처리하며, xv6 입장에서 LLM 호출은 IPC 요청으로 추상화된다. 본 프로젝트는 LLM API를 **자원으로 취급해 xv6 커널이 직접 관리**하는 데 초점이 있으며, 기존 OS 라이브러리를 호출하는 것이 아니라 OS 개념 자체를 설계·구현한다.

### 1.2 선택한 방향: **A (OS for LLM)**

가이드라인 §2의 방향 A — "LLM을 호스팅·서빙·오케스트레이션하는 OS, 런타임 계층, 또는 에이전트 플랫폼"에 해당. 구체적으로는 **"여러 개의 동시 LLM 프로세스(에이전트)를 CPU/메모리/도구 쿼터를 공정하게 할당하며 관리하는 미니 OS"** 범주.

### 1.3 기존 문서와의 관계

| 문서 | 역할 |
|---|---|
| `README.md` | OS 메커니즘 **카탈로그**. 기술 부록으로 재활용. |
| `CLAUDE.md` | Claude Code 운영 매뉴얼. 본 문서를 SSoT로 참조. |
| `HARNESS.md` | CC 자율 작동 하네스 설계. 본 문서의 §11~§20을 작업 큐로 사용. |
| **본 문서 (`MASTER_PLAN.md`)** | **결정·실행·일정 레이어**. xv6 기반 전면 재작성, 작업 큐 흡수. |

---

## 2. 킬러 시나리오 — 로그 트리아지 파이프라인

### 2.1 채택 이유

가이드라인 §2의 **필수 제약**은 "OS 컴포넌트가 단지 'LLM이 그 위에 돌아간다'는 정도로는 안 된다"고 명시한다. 로그 트리아지는 다음 이유로 xv6 OS 결정이 **substantive**하게 들어간다.

- **대량 입력**: 수천 줄 로그를 다루므로 병렬화 가치(speedup 2x+)가 자연스럽게 정당화됨
- **격리 필요성**: 로그 한 줄 파싱 실패가 전체 파이프라인을 죽이면 안 됨 → xv6 **프로세스 격리**가 단순 장식이 아니라 요구사항
- **품질 검증 필요성**: LLM 출력이 비결정적이므로 Evaluator가 검증하고 재시도를 지시하는 루프가 필요 → **Supervisor 패턴**이 기능적 의미를 가짐
- **우선순위**: critical 등급 로그는 먼저 처리해야 함 → xv6 **스케줄러 수정**이 명확한 가치

### 2.2 에이전트 구성

```
[Raw Log Stream]
       │
       ▼
┌────────────────┐
│ parser         │  로그 라인 → 구조화된 구조체
│  (xv6 proc)   │  (Upstage Solar Pro 3 via Proxy)
└────────┬───────┘
         │ pipe
         ▼
┌────────────────┐
│ classifier     │  레벨/카테고리 분류
│  (xv6 proc)   │  (INFO/WARN/ERROR/CRITICAL)
└────────┬───────┘
         │ pipe
         ▼
┌────────────────┐
│ root-cause     │  ERROR/CRITICAL만 진단
│  (xv6 proc)   │
└────────┬───────┘
         │ pipe
         ▼
┌────────────────┐
│ fix-suggester  │  수정 제안 생성
│  (xv6 proc)   │
└────────┬───────┘
         │ pipe
         ▼
┌────────────────┐     재시도 신호 (pipe)
│  evaluator     │ ──────────────────────▶ (해당 에이전트로 피드백)
│  (xv6 proc)   │  품질 검증 + 재시도 판단
└────────┬───────┘
         │
         ▼
   [Triage Report]
```

총 5종류의 에이전트 프로세스. Evaluator는 각 단계의 출력을 검증하고 품질 미달 시 해당 에이전트에 재시도 신호를 보낸다.

### 2.3 입력/출력 정의

- **입력**: 호스트 Linux에서 xv6 게스트로 전달되는 로그 파일 (virtio/pipe 경유)
- **출력**: 구조화된 트리아지 결과 (xv6 파일시스템에 저장)
- **시연 시나리오**: xv6 셸에서 `triage kernel-dmesg.log` 실행 → 에이전트들이 병렬 처리 → AgentMonitor가 실시간 메트릭 표시 → 리포트 생성

---

## 3. 시스템 아키텍처

### 3.1 Host-Guest 분리 아키텍처

xv6는 네트워크 스택이 없으므로 LLM API 호출은 QEMU 호스트의 Proxy Daemon이 처리한다.

```
┌──────────────────────────────────────────────────────┐
│                  QEMU Host (Linux)                   │
│                                                      │
│  ┌───────────────────────────────┐  ┌─────────────┐  │
│  │         xv6 Guest             │  │ LLM Proxy   │  │
│  │                               │  │ Daemon      │  │
│  │  ┌─────────────────────────┐  │  │             │  │
│  │  │   Orchestrator (proc 1) │  │  │ - API 호출  │  │
│  │  │   - fork agents         │◄─┼─►│ - 결과 반환 │  │
│  │  │   - 스케줄러 우선순위    │  │  │ - 큐 관리   │  │
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

xv6 입장에서 LLM 호출은 **Proxy Daemon으로 보내는 IPC 요청**으로 추상화된다.

### 3.2 OS 개념 매핑표

가이드라인 §2 "OS 개념이 substantive하게 포함" 요구를 충족하기 위해 다음 OS 개념을 **xv6 커널에서 직접 설계·구현**한다.

| # | 컴포넌트 | OS 개념 | xv6 구현 수단 |
|---|---|---|---|
| 1 | Orchestrator | **프로세스 관리** | xv6 `fork()`, `proc` 구조체 확장 |
| 2 | 에이전트 간 통신 | **IPC (pipe)** | xv6 `pipe` 직접 구현/수정 |
| 3 | Evaluator ↔ Worker | **IPC (신호)** | xv6 `kill()`, `signal` 메커니즘 |
| 4 | 공유 컨텍스트 | **동기화** | xv6 `spinlock`, `sleep/wakeup` |
| 5 | 에이전트 우선순위 | **스케줄링** | xv6 스케줄러 수정 (우선순위 큐 추가) |
| 6 | 장애 격리 | **프로세스 격리** | xv6 프로세스 독립 주소 공간 |
| 7 | 컨텍스트 저장 | **파일시스템** | xv6 파일시스템에 에이전트 상태 저장 |
| 8 | 모니터링 | **시스템 콜** | xv6 시스템 콜 추가 (`agentstat`) |

> 위 모든 항목은 Python 라이브러리나 Linux 기능을 **호출**하는 것이 아니라, xv6 커널 코드를 **직접 수정·추가**하여 구현한다. 이것이 기존 계획(multiprocessing, cgroups 활용)과의 근본적 차이다.

### 3.3 Supervisor 패턴 (Evaluator 루프)

```
Worker 에이전트                    Evaluator 에이전트
      │                                  │
      │──── 결과 전달 (pipe write) ──────▶│
      │                                  │ 품질 검증
      │                                  │ (Upstage API 호출)
      │◀─── 재시도 신호 (kill/signal) ────│ 미달 시
      │                                  │
      │ 재실행                           │
      │──── 새 결과 전달 ────────────────▶│
      │                                  │ 통과
      │                            다음 단계로
```

xv6의 `kill()` + `sleep/wakeup` 메커니즘으로 구현. Evaluator가 Worker에게 재시도를 지시하고, Worker는 신호를 받아 재실행한다.

### 3.4 시퀀스 다이어그램

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
  │          │            │        │           │             │         │─검증──────▶│
  │          │            │        │           │             │         │◀──결과─────│
  │          │            │        │           │             │         │            │
  │          │            │        │◀──signal(retry)─────────────────── │           │
  │          │            │        │ 재실행    │             │          │           │
  │          │◀─────────────────────────────────────────────────────────│           │
  │◀─report──│            │        │           │             │          │           │
```

---

## 4. 구현 방법 결정

### 4.1 xv6 커널 수정 범위

구현의 핵심은 xv6 소스코드를 직접 수정하는 것이다. 수정 대상 파일과 내용은 다음과 같다.

| xv6 파일 | 수정 내용 |
|---|---|
| `proc.h` | `proc` 구조체에 `agent_role`, `priority`, `agent_state` 필드 추가 |
| `proc.c` | `fork()` 수정 — 에이전트 역할 상속, 우선순위 초기화 |
| `sched.c` | 스케줄러 수정 — Round Robin → 우선순위 기반 스케줄링 |
| `pipe.c` | 에이전트 간 통신용 pipe 버퍼 크기 조정, 블로킹 동작 확인 |
| `spinlock.c` | 공유 컨텍스트 접근 시 사용할 lock 확인 및 필요 시 확장 |
| `syscall.c/h` | `agentstat` 시스템 콜 추가 (에이전트 상태 조회용) |
| `file.c` | 에이전트 컨텍스트 파일시스템 저장/복원 |

### 4.2 QEMU Host-Guest 통신

xv6 게스트와 Linux 호스트의 Proxy Daemon은 virtio serial 또는 공유 파이프로 통신한다.

```
xv6 에이전트          virtio/pipe          Linux Proxy Daemon
      │                                          │
      │─── LLM 요청 (직렬화된 구조체) ──────────▶│
      │                                          │─── Upstage API 호출
      │                                          │◀── API 응답
      │◀── LLM 응답 (직렬화된 구조체) ───────────│
```

xv6 입장에서는 단순한 read/write 시스템 콜. LLM의 존재를 몰라도 된다.

### 4.3 채택하지 않은 방법

| 방법 | 채택 안 한 이유 |
|---|---|
| Python multiprocessing | OS 기능을 **호출**하는 것 — "직접 구현" 조건 미충족 |
| Linux cgroups 사용 | Linux 기능을 **사용**하는 것 — "직접 설계" 조건 미충족 |
| Linux threading | 동일한 이유 |

---

## 5. LLM 백엔드 — Upstage Solar Pro 3

### 5.1 Proxy Daemon 구조

xv6는 직접 HTTP 호출 불가. Linux 호스트에서 Proxy Daemon이 중계한다.

```python
# host/proxy_daemon.py (Linux 호스트에서 실행)
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

# virtio serial 또는 named pipe로 xv6와 통신
# 요청: JSON 직렬화된 {role, prompt}
# 응답: JSON 직렬화된 {result}
```

### 5.2 API 키 관리

```bash
# .env (git에 절대 커밋 금지)
UPSTAGE_API_KEY=up-xxxxxxxxxxxxxxxxxxxx

# .gitignore
.env
.env.*
*.key
out/
.cache/llm/
```

### 5.3 Rate Limit 대응 — 지수 백오프 + 캐시

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

**평가 모드 옵션**:
- `--mode live`: 실 API 호출 (시연용)
- `--mode replay`: 캐시만 사용 (평가 측정용, 재현성 보장)
- `--mode mock`: `time.sleep` + 더미 응답 (단위 테스트용)

---

## 6. 평가 계획

### 6.1 평가 지표

| 지표 | 측정 방법 | 목표 | 반복 |
|---|---|---|---|
| **병렬 처리 효과 (speedup)** | 순차 wall-clock / 병렬 wall-clock | **2x 이상** | 5회 평균 ± stdev |
| **장애 격리** | 에이전트 프로세스 kill 후 나머지 생존 비율 | **100%** | 10회 |
| **Evaluator 재시도 효과** | 재시도 전/후 출력 품질 점수 비교 | 품질 향상 확인 | 20건 샘플 |
| **우선순위 스케줄링 효과** | CRITICAL vs INFO 로그 처리 시간 차이 | 비대칭 분배 확인 | 1회 (60초) |
| **스케줄러 수정 전/후 비교** | Round Robin vs 우선순위 스케줄러 처리 시간 | 정량 보고 | 5회 평균 |

### 6.2 통제 변수

- **LLM 응답**: `--mode replay` (캐시된 응답만 사용)
- **입력 로그**: 고정된 100줄 로그 셋
- **에이전트 수**: 5개 (parser/classifier/root-cause/fix-suggester/evaluator)
- **하드웨어**: 동일 머신, QEMU 설정 고정
- **반복**: 각 실험 5회 이상, 평균과 표준편차 보고

### 6.3 측정 자동화

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

## 7. Week 10~14 일정 개괄

> 주차별 Done의 정의. 작업 단위 T-NN의 상세는 Part II 참조.

### Week 10 — 시스템 스케치

- [ ] 본 문서를 팀원 전원이 리뷰하고 §2(시나리오) 최종 확정
- [ ] xv6 빌드 환경 셋업 (QEMU + xv6 소스 클론, 빌드 확인)
- [ ] 블록 다이어그램(§3.1)을 draw.io로 작성, 저장소에 PNG 커밋
- [ ] Proxy Daemon `hello-upstage.py` — Upstage API 호출 동작 확인
- [ ] **공식 제안서 1문단**(§1.1) 강사 제출
- [ ] 저장소 README.md를 가이드 §3 형식으로 재구성
- [ ] **역할 분담 확정** (본 문서 §10)

### Week 11 — MVP

- [ ] xv6 `proc.h`에 `agent_role`, `priority` 필드 추가
- [ ] xv6에서 `fork()`로 3개 에이전트 프로세스 생성, pipe로 연결
- [ ] Proxy Daemon ↔ xv6 virtio 통신 end-to-end 동작
- [ ] parser 에이전트가 Proxy를 통해 Upstage API 호출 후 결과 반환
- [ ] 에이전트 1개 kill → Orchestrator 생존 확인
- [ ] `--mode mock`으로 CI 통과

### Week 12 — 통합 + 스케줄러 수정

- [ ] 5개 에이전트 모두 동작 (parser/classifier/root-cause/fix-suggester/evaluator)
- [ ] xv6 스케줄러 수정 완료 — 우선순위 큐 기반
- [ ] Evaluator ↔ Worker 재시도 루프 동작 (`kill` + `sleep/wakeup`)
- [ ] `agentstat` 시스템 콜 추가 및 동작 확인
- [ ] AgentMonitor 가동, 메트릭 기록
- [ ] **영어 슬라이드 골격** 시작 (목차만이라도)

### Week 13 — 실험 + 보고서 + 드라이런

- [ ] `bench/run_all.sh` 실행 완료, `out/REPORT.md` 생성
- [ ] 5가지 실험 모두 목표 달성 또는 미달 시 사유 분석
- [ ] **Technical Report** 작성 완료
- [ ] **Development Process Document** (`PROCESS.md`) 작성
- [ ] **영어 슬라이드** 1차 완성
- [ ] 드라이런 1회 (15분 발표 시간 측정)

### Week 14 — 최종 발표

- [ ] 슬라이드 최종본, 데모 GIF/비디오
- [ ] 발표 (Professor 15% + Peer review 15%)
- [ ] 저장소 README 최종 정리, 데모 스크린샷 추가

---

## 8. 산출물 매핑 (가이드 §5)

| # | 가이드 §5 산출물 | 본 프로젝트 위치 |
|---|---|---|
| 1 | **Application** | xv6 소스 수정본 (`xv6-src/`), Proxy Daemon (`host/`), `bench/`, 데모 GIF (`docs/demo.gif`) |
| 2 | **Technical Report** | `docs/TECHNICAL_REPORT.md` + `out/REPORT.md` (실험 결과) |
| 3 | **Development Process Document** | `PROCESS.md`, `docs/meetings/` 폴더 |
| 4 | **Presentation Slides** (영어) | `slides/final.pptx` (Week 12부터 작성, Week 14 최종) |

---

## 9. 위험과 대응

| 위험 | 신호 | 대응 |
|---|---|---|
| **xv6 네트워크 스택 부재** | xv6에서 직접 API 호출 불가 | §3.1 Host-Guest 아키텍처로 해결. Proxy Daemon이 중계. |
| **virtio 통신 구현 난이도** | xv6 ↔ 호스트 통신이 Week 11에 안 됨 | 초기에는 QEMU `-chardev`로 단순 serial 통신. 안 되면 공유 파일시스템(임시) fallback. |
| **"얇은 래퍼" 오해** | 보고서가 "Proxy가 API 호출한 것"으로 읽힘 | Technical Report §3.2 OS 매핑표를 첫 장에 배치. xv6 커널 수정 커밋을 증거로 제시. |
| **xv6 스케줄러 수정 난이도** | Week 12에 스케줄러가 안 됨 | Round Robin 유지 + `agentstat`으로 측정만 먼저. Week 13에 우선순위 추가. |
| **LLM 비결정성** | speedup 측정값이 매번 달라짐 | `--mode replay`로 캐시된 응답만 사용. 실 API는 시연 시에만. |
| **Evaluator 재시도 루프 무한 반복** | 품질 기준 미달 시 무한 재시도 | 최대 재시도 횟수(3회) 하드코딩. 초과 시 Orchestrator에 실패 보고. |
| **영어 발표 지연** | Week 13에 슬라이드가 없음 | Week 12부터 코드 주석과 보고서 핵심 문장을 영어로 작성. |

---

## 10. 역할 분담

> **TBD — Week 10 회의에서 확정**

가이드라인 §1에 따라 4인 팀. 권장 분담:

| 역할 | 책임 영역 | 주요 산출물 |
|---|---|---|
| **팀 리더 / 오케스트레이션** | 단일 연락 창구, xv6 Orchestrator 프로세스, 일정 관리 | 일정표, 회의록, 통합 빌드 |
| **xv6 커널** | `proc.h`, `sched.c`, `spinlock`, `syscall` 수정 | §3.2 매핑표의 1, 4, 5, 6, 8번 |
| **에이전트 / LLM** | Proxy Daemon, 5개 에이전트 구현, IPC pipe 연결 | §5 전체, 에이전트 모듈, virtio 통신 |
| **평가 / 모니터링** | AgentMonitor, `bench/run_all.sh`, 영어 슬라이드 | §6 전체, 실험 결과, slides/ |

3인 팀 예외 적용 시: 평가/모니터링을 xv6 커널과 합침.

---

# Part II — 작업 일정 (T-NN 작업 큐)

> Claude Code 자율 작동 시 다음 작업을 결정하는 근거 문서.
> `CLAUDE.md` §2 작업 흐름에 따라 한 번에 하나의 작업만 `[~]` 상태로 둔다.

## 11. 작업 형식과 상태 표기

### 11.1 상태 표기
- `[ ]` **TODO** — 아직 시작 안 함
- `[~]` **IN PROGRESS** — CC가 작업 중 (전체 worktree에서 한 번에 하나만)
- `[x]` **DONE** — 검증 통과, 커밋 완료
- `[!]` **BLOCKED** — `BLOCKED.md` 참조. 사람 개입 필요
- 🔴 **HUMAN GATE** — 시작 전 반드시 사람 승인

### 11.2 작업 메타데이터
각 작업은 다음 메타데이터를 포함한다:
- **depends**: 선행 작업 ID 목록. 모두 `[x]`여야 시작 가능
- **files**: 건드릴 파일 목록. 이 외의 파일을 수정하려면 BLOCKED 처리
- **verify**: 검증 명령. 통과해야 `[x]`로 변경 가능
- **estimate**: 예상 소요 (CC 자율 시간 기준)
- **assignee**: 담당 worktree (4인 팀 + 4 worktree 운영 시)

---

## 12. Phase 1 — 하네스 부트스트랩 (Week 10)

> 목표: CC 자율 작동의 기반 인프라 구축. 이 Phase가 끝나면 이후 Phase는 거의 자율로 돈다.

### T-01 `[x]` `.claude/settings.json` 작성
- **depends**: 없음
- **files**: `.claude/settings.json`
- **verify**: CC 재시작 후 `sudo` 명령 거부 확인
- **estimate**: 10분
- **assignee**: harness
- **note**: allowed_tools / disallowed_tools 설정. `sudo`, `git push`, `rm -rf /` 차단

### T-02 `[x]` `.env.example` 및 `.gitignore` 정비
- **depends**: 없음
- **files**: `.env.example`, `.gitignore`
- **verify**: `.env`가 git에 추적되지 않는지 확인 (`git check-ignore .env`)
- **estimate**: 10분
- **assignee**: harness

### T-03 `[x]` xv6 빌드 환경 검증
- **depends**: 없음
- **files**: 없음 (검증만)
- **verify**: `cd xv6-src && make qemu` 실행, xv6 셸 프롬프트 `$ ` 도달 후 `Ctrl-A X`로 종료
- **estimate**: 30분
- **assignee**: harness

### T-04 `[x]` 호스트 측 `hello-upstage.py` 작성
- **depends**: T-02
- **files**: `host/hello_upstage.py`, `host/requirements.txt`
- **verify**: `MODE=live python host/hello_upstage.py` → solar-pro 응답 1줄 출력
- **estimate**: 30분
- **assignee**: agent

### T-05 `[x]` `tests/autotest.sh` 골격
- **depends**: T-03
- **files**: `tests/autotest.sh`, `tests/inputs/smoke.in`, `Makefile`
- **verify**: `make autotest` 실행 → 60초 이내 `PASS` 또는 `FAIL` 출력
- **estimate**: 2시간
- **assignee**: harness
- **note**: 헤드리스 QEMU + serial 캡처 + grep 판정. xv6 안에 `smoketest` 유저 프로그램 추가 필요

### T-06 `[x]` xv6 user `smoketest.c` 추가
- **depends**: T-03
- **files**: `xv6-src/user/smoketest.c`, `xv6-src/Makefile`
- **verify**: xv6 셸에서 `smoketest` 실행 → `SMOKE_TEST_PASS` 출력
- **estimate**: 30분
- **assignee**: kernel
- **note**: fork/wait/pipe 기본 동작 확인 후 PASS 출력

### T-07 `[x]` `tests/regression.sh` 골격
- **depends**: T-05, T-06
- **files**: `tests/regression.sh`
- **verify**: `make regression` → autotest + 기본 셸 명령 확인 + Proxy hello
- **estimate**: 1시간
- **assignee**: harness

### T-08 `[x]` `bench/summarize.py` 골격
- **depends**: T-02
- **files**: `bench/summarize.py`
- **verify**: 더미 JSON 5개 입력 시 평균·stdev 한 줄 JSON 출력
- **estimate**: 30분
- **assignee**: bench

### T-09 `[x]` 패닉 dump 매크로 추가
- **depends**: T-03, T-06
- **files**: `xv6-src/kernel/printf.c`, `xv6-src/kernel/proc.h`
- **verify**: 일부러 패닉 유발 후 `[PANIC_DUMP_BEGIN]...[PANIC_DUMP_END]` 출력 확인
- **estimate**: 1시간
- **assignee**: kernel

### T-10 `[x]` `AGENT_LOG` 매크로 도입
- **depends**: T-09
- **files**: `xv6-src/kernel/agent_log.h`, `xv6-src/kernel/defs.h`
- **verify**: 커널에서 `AGENT_LOG("info", "test %d", 42)` 호출 시 `[AGENT][info][...]` 출력
- **estimate**: 30분
- **assignee**: kernel

---

## 13. Phase 2 — xv6 proc 확장 + virtio (Week 11 전반)

> 목표: 에이전트 메타데이터를 xv6 proc에 박고, 호스트와 통신하는 채널 확보.

### T-20 `[x]` proc 구조체 확장
- **depends**: T-06, T-10
- **files**: `xv6-src/kernel/proc.h`, `xv6-src/kernel/proc.c`
- **verify**: fork 후 자식 proc의 `agent_role`, `priority` 필드가 부모로부터 상속됨 (테스트: `xv6-src/user/test_proc_fields.c`)
- **estimate**: 2시간
- **assignee**: kernel
- **note**: 기본값은 `agent_role = "none"`, `priority = 0`

### T-21 `[x]` `setrole` 시스템 콜 추가
- **depends**: T-20
- **files**: `xv6-src/kernel/syscall.c`, `xv6-src/kernel/syscall.h`, `xv6-src/kernel/sysproc.c`, `xv6-src/user/user.h`, `xv6-src/user/usys.pl`
- **verify**: 유저 프로그램에서 `setrole("parser")` 호출 후 `agentstat`에 반영됨
- **estimate**: 1시간
- **assignee**: kernel

### T-22 `[x]` `agentstat` 시스템 콜 + 유저 프로그램
- **depends**: T-21
- **files**: 위와 동일 + `xv6-src/user/agentstat.c`
- **verify**: xv6 셸에서 `agentstat` 실행 시 모든 active proc의 정보를 JSON 1줄로 출력
- **estimate**: 2시간
- **assignee**: kernel

### T-23 `[x]` virtio serial 채널 설정 (console-serial fallback path)
- **depends**: T-04
- **files**: `Makefile` (QEMU 옵션), `host/proxy_pipe.py`
- **verify**: 호스트 측 `host/proxy_pipe.py`가 xv6 게스트의 출력을 받고 입력을 보낼 수 있음 (echo 테스트)
- **estimate**: 3시간
- **assignee**: agent
- **note**: 가장 까다로운 작업. virtio가 안 되면 `-chardev pipe`로 fallback

### T-24 `[x]` xv6 측 proxy client 유저 라이브러리
- **depends**: T-23
- **files**: `xv6-src/user/proxy_client.c`, `xv6-src/user/proxy_client.h`
- **verify**: xv6 유저 프로그램에서 `proxy_call("echo", "hello")` → 호스트 echo 응답 받음
- **estimate**: 2시간
- **assignee**: agent

### T-25 `[x]` host proxy daemon mock 모드
- **depends**: T-23
- **files**: `host/proxy_daemon.py`
- **verify**: `MODE=mock python host/proxy_daemon.py` 후 xv6에서 LLM 요청 시 더미 응답 받음
- **estimate**: 1시간
- **assignee**: agent

### T-26 `[x]` host proxy daemon live 모드 (Upstage 통합)
- **depends**: T-25, T-04
- **files**: `host/proxy_daemon.py`
- **verify**: `MODE=live`로 xv6에서 짧은 LLM 요청 → solar-pro 응답
- **estimate**: 1시간
- **assignee**: agent

### T-27 `[x]` LLM 응답 캐시 (replay 모드)
- **depends**: T-26
- **files**: `host/proxy_daemon.py`, `.cache/llm/`
- **verify**: 동일 입력 두 번 호출 시 두 번째는 캐시 사용 (timestamp 비교로 확인)
- **estimate**: 1시간
- **assignee**: agent

---

## 14. Phase 3 — 에이전트 5종 + IPC pipe (Week 11 후반 ~ Week 12 전반)

> 목표: 5종 에이전트가 xv6 안에서 pipe로 연결되어 동작.

### T-30 `[x]` parser 에이전트 (xv6 유저 프로그램)
- **depends**: T-22, T-24
- **files**: `xv6-src/user/agent_parser.c`
- **verify**: 표준입력으로 로그 라인 받아 proxy_call 후 구조화된 출력
- **estimate**: 2시간
- **assignee**: agent

### T-31 `[x]` classifier 에이전트
- **depends**: T-30
- **files**: `xv6-src/user/agent_classifier.c`
- **verify**: parser 출력 → 분류 결과 (INFO/WARN/ERROR/CRITICAL) 출력
- **estimate**: 1.5시간
- **assignee**: agent

### T-32 `[x]` root-cause 에이전트
- **depends**: T-31
- **files**: `xv6-src/user/agent_rootcause.c`
- **verify**: ERROR/CRITICAL 입력만 처리, 그 외는 패스스루
- **estimate**: 1.5시간
- **assignee**: agent

### T-33 `[x]` fix-suggester 에이전트
- **depends**: T-32
- **files**: `xv6-src/user/agent_fixsuggest.c`
- **verify**: root-cause 출력 받아 수정 제안 생성
- **estimate**: 1.5시간
- **assignee**: agent

### T-34 `[x]` evaluator 에이전트
- **depends**: T-33
- **files**: `xv6-src/user/agent_evaluator.c`
- **verify**: 각 단계 출력을 검증 후 통과/재시도 신호 출력
- **estimate**: 3시간
- **assignee**: agent

### T-35 `[x]` orchestrator (triage 명령)
- **depends**: T-30, T-31, T-32, T-33, T-34
- **files**: `xv6-src/user/triage.c`
- **verify**: `triage samples/short.log` 실행 시 5개 에이전트 fork + pipe 연결 + 결과 출력
- **estimate**: 2시간
- **assignee**: agent

### T-36 `[x]` 첫 end-to-end mock 통과
- **depends**: T-35
- **files**: `tests/e2e_mock.sh`
- **verify**: `MODE=mock make e2e` 통과
- **estimate**: 1시간
- **assignee**: bench

---

## 15. Phase 4 — Evaluator 재시도 루프 (Week 12 중반)

### T-40 `[x]` `kill` + `sleep/wakeup`로 재시도 신호
- **depends**: T-34, T-35
- **files**: `xv6-src/kernel/proc.c` (필요 시 sleep/wakeup 확장), `xv6-src/user/agent_evaluator.c`
- **verify**: Evaluator가 worker에게 retry 신호 보내면 worker가 재실행
- **estimate**: 4시간
- **assignee**: kernel + agent (협업)

### T-41 `[x]` 재시도 카운터 + 최대 3회 제한
- **depends**: T-40
- **files**: `xv6-src/user/agent_evaluator.c`
- **verify**: 일부러 검증 실패하는 mock 응답으로 정확히 3회 재시도 후 실패 보고
- **estimate**: 1시간
- **assignee**: agent

---

## 16. Phase 5 — 스케줄러 수정 (Week 12 후반) 🔴

> 이 Phase는 모두 HUMAN GATE. xv6 부팅 자체를 깰 수 있음.

### T-50 `[x]` 🔴 스케줄러 백업 브랜치 생성
- **depends**: T-36
- **files**: (사람이 수행) `git checkout -b backup/before-scheduler`
- **verify**: 백업 브랜치 존재 확인
- **estimate**: 5분
- **assignee**: human

### T-51 `[x]` 🔴 sched.c 우선순위 큐 구조 추가
- **depends**: T-50
- **files**: `xv6-src/kernel/proc.c`의 `scheduler()` 함수
- **verify**: `make autotest` 통과 (회귀 없음) + 우선순위 적용 테스트 통과
- **estimate**: 4시간 (이 중 사람 검토 1시간)
- **assignee**: kernel (사람 승인 후)

### T-52 `[x]` 🔴 priority 측정 시스템 콜
- **depends**: T-51
- **files**: `xv6-src/kernel/sysproc.c`, `xv6-src/user/user.h`
- **verify**: 같은 워크로드를 다른 priority로 실행 시 처리 시간 차이 측정
- **estimate**: 2시간
- **assignee**: kernel

---

## 17. Phase 6 — 평가 (Week 13)

### T-60 `[x]` `bench/run_all.sh` 완성
- **depends**: T-36, T-41, T-52
- **files**: `bench/run_all.sh`
- **verify**: 5개 실험 5회 반복 후 `out/bench/*.json` 생성
- **estimate**: 2시간
- **assignee**: bench

### T-61 `[x]` `bench/report.py` — REPORT.md 자동 생성
- **depends**: T-60
- **files**: `bench/report.py`
- **verify**: `python bench/report.py out/bench/ > out/REPORT.md` → 표 + 통계 포함된 마크다운
- **estimate**: 2시간
- **assignee**: bench

### T-62 실 벤치마크 실행 (사람만)
- **depends**: T-61
- **files**: 없음
- **verify**: `bash bench/run_all.sh` 완전 실행
- **estimate**: 1시간 (사람 감독 하)
- **assignee**: human

---

## 18. Phase 7 — 산출물 (Week 13~14)

### T-70 `[x]` Technical Report 초안
- **depends**: T-62
- **files**: `docs/TECHNICAL_REPORT.md`
- **verify**: 5,000~8,000 단어, §3.2 OS 매핑표가 첫 장에 포함
- **estimate**: 4시간 (Opus 권장)
- **assignee**: human + claude

### T-71 `[~]` Development Process Document
- **depends**: T-70
- **files**: `PROCESS.md`
- **verify**: 주차별 회의록 + 의사결정 + 이슈 해결 포함
- **estimate**: 2시간
- **assignee**: human + claude

### T-72 영어 슬라이드 초안
- **depends**: T-70
- **files**: `slides/draft.md` (마크다운 → pptx)
- **verify**: 15분 발표 분량, 영어 only
- **estimate**: 3시간
- **assignee**: human + claude

### T-73 데모 GIF
- **depends**: T-36
- **files**: `docs/demo.gif`
- **verify**: triage 명령 시연 GIF 파일 존재
- **estimate**: 1시간
- **assignee**: human

---

## 19. 진행 현황 요약

**마지막 갱신**: 2026-05-26

**완료된 작업** (`[x]`):
- Phase 1: T-01~T-10 (10건)
- Phase 2: T-20~T-27 (8건)
- Phase 3: T-30~T-36 (7건)
- Phase 4: T-40, T-41 (2건)
- Phase 5: T-50~T-52 (3건) 🔴
- Phase 6: T-60, T-61 (2건)
- Phase 7: T-70 (1건)

**남은 작업**:
| ID | 작업 | 담당 |
|---|---|---|
| T-62 | 실 벤치마크 실행 | human |
| T-71 | Development Process Document (`PROCESS.md`) | human + claude |
| T-72 | 영어 슬라이드 초안 | human + claude |
| T-73 | 데모 GIF | human |

`docs/TECHNICAL_REPORT.md` Technical Report 초안 5,007 단어 완료. 본문 영문 번역(이전 `ImplementationPlan.en.md`)도 본 문서 영문판(`MASTER_PLAN.en.md`)으로 흡수됨.

---

## 20. 작업 추가 규칙

새 작업을 추가할 때:
1. 새 Phase면 Phase 번호를 잇는다 (Phase 8, 9...).
2. 같은 Phase면 다음 사용 가능한 T-NN 번호.
3. 의존성을 정확히 명시한다 (잘못된 의존성은 CC가 wedge에 빠지게 한다).
4. `verify` 명령이 **반드시 자동 실행 가능**해야 한다. "사람이 보기 좋은지 확인" 같은 건 verify가 아니다.

---

## 부록 A — 채택하지 않은 구현 방법

- **A.1 Python multiprocessing**: OS 기능을 호출하는 것이라 "직접 구현" 조건 미충족. 가이드라인의 "얇은 래퍼" 경고에 해당.
- **A.2 Linux cgroups**: Linux 커널 기능을 사용하는 것. xv6에서 직접 자원 관리를 구현하는 본 프로젝트의 방향과 불일치.
- **A.3 소켓 / gRPC**: 단일 머신 데모에서는 과도한 복잡성. xv6 내부 IPC로 충분.

---

## 부록 B — 셀프 체크리스트 (가이드 §3 README 요구사항)

- [ ] 한 문단 프로젝트 요약 + 방향 (A)
- [ ] 기술 스택 개요 (xv6, QEMU, Python Proxy, Upstage Solar Pro 3)
- [ ] 셋업 안내 (QEMU 설치, xv6 빌드, `.env`, Upstage API 키)
- [ ] 실행 방법 (`make qemu` → xv6 셸 → `triage ...`)
- [ ] 데모 스크린샷 / GIF
- [ ] 저장소 public 설정 확인
- [ ] API 키가 git에 커밋되지 않았는지 확인
