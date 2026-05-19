# 구현 계획서 — LLM 멀티에이전트 OS (xv6 기반)

> 본 문서는 GuideLine.md의 요구사항을 충족하면서 xv6 커널을 직접 수정하여
> LLM 에이전트 오케스트레이션 런타임을 구현하는 결정 문서입니다.
>
> 가이드라인 §5의 산출물 #2(Technical Report)와 #3(Development Process Document)의 통합 초안 역할을 겸합니다.

---

## 목차

1. [프로젝트 개요](#1-프로젝트-개요)
2. [킬러 시나리오 — 로그 트리아지 파이프라인](#2-킬러-시나리오--로그-트리아지-파이프라인)
3. [시스템 아키텍처](#3-시스템-아키텍처)
4. [구현 방법 결정](#4-구현-방법-결정)
5. [LLM 백엔드 — Upstage Solar Pro 3](#5-llm-백엔드--upstage-solar-pro-3)
6. [평가 계획](#6-평가-계획)
7. [일정 — Week 10~14](#7-일정--week-1014)
8. [산출물 매핑](#8-산출물-매핑-가이드-5)
9. [위험과 대응](#9-위험과-대응)
10. [역할 분담](#10-역할-분담)

---

## 1. 프로젝트 개요

### 1.1 한 문단 요약 (가이드 §3 요구)

**Liberal_OS**팀은 xv6 커널을 직접 수정하여 여러 LLM 에이전트를 **OS 레벨의 격리·스케줄링·자원 관리 메커니즘**으로 오케스트레이션하는 시스템을 구현한다. 각 에이전트(parser / classifier / root-cause / fix-suggester / evaluator)는 xv6 프로세스로 표현되며, 오케스트레이터는 직접 구현한 IPC(pipe)로 에이전트 간 통신을 수행하고, 직접 수정한 스케줄러로 에이전트별 우선순위를 부여한다. Evaluator 에이전트는 Worker 에이전트의 출력을 검증하고 품질 미달 시 재시도 신호를 보내는 Supervisor 패턴을 구현한다. 실제 LLM API 호출은 QEMU 호스트의 Proxy Daemon이 처리하며, xv6 입장에서 LLM 호출은 IPC 요청으로 추상화된다. 본 프로젝트는 LLM API를 **자원으로 취급해 xv6 커널이 직접 관리**하는 데 초점이 있으며, 기존 OS 라이브러리를 호출하는 것이 아니라 OS 개념 자체를 설계·구현한다.

### 1.2 선택한 방향: **A (OS for LLM)**

가이드라인 §2의 방향 A — "LLM을 호스팅·서빙·오케스트레이션하는 OS, 런타임 계층, 또는 에이전트 플랫폼"에 해당. 구체적으로는 **"여러 개의 동시 LLM 프로세스(에이전트)를 CPU/메모리/도구 쿼터를 공정하게 할당하며 관리하는 미니 OS"** 범주.

### 1.3 기존 문서와의 관계

| 문서 | 역할 |
|---|---|
| README.md | OS 메커니즘 **카탈로그**. 기술 부록으로 재활용. |
| **본 문서 (ImplementationPlan.md)** | **결정·실행 레이어**. xv6 기반으로 전면 재작성. |

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

## 7. 일정 — Week 10~14

### Week 10 — 시스템 스케치

**Done의 정의**:
- [ ] 본 문서를 팀원 전원이 리뷰하고 §2(시나리오) 최종 확정
- [ ] xv6 빌드 환경 셋업 (QEMU + xv6 소스 클론, 빌드 확인)
- [ ] 블록 다이어그램(§3.1)을 draw.io로 작성, 저장소에 PNG 커밋
- [ ] Proxy Daemon `hello-upstage.py` — Upstage API 호출 동작 확인
- [ ] **공식 제안서 1문단**(§1.1) 강사 제출
- [ ] 저장소 README.md를 가이드 §3 형식으로 재구성
- [ ] **역할 분담 확정** (본 문서 §10)

### Week 11 — MVP

**Done의 정의**:
- [ ] xv6 `proc.h`에 `agent_role`, `priority` 필드 추가
- [ ] xv6에서 `fork()`로 3개 에이전트 프로세스 생성, pipe로 연결
- [ ] Proxy Daemon ↔ xv6 virtio 통신 end-to-end 동작
- [ ] parser 에이전트가 Proxy를 통해 Upstage API 호출 후 결과 반환
- [ ] 에이전트 1개 kill → Orchestrator 생존 확인
- [ ] `--mode mock`으로 CI 통과

### Week 12 — 통합 + 스케줄러 수정

**Done의 정의**:
- [ ] 5개 에이전트 모두 동작 (parser/classifier/root-cause/fix-suggester/evaluator)
- [ ] xv6 스케줄러 수정 완료 — 우선순위 큐 기반
- [ ] Evaluator ↔ Worker 재시도 루프 동작 (`kill` + `sleep/wakeup`)
- [ ] `agentstat` 시스템 콜 추가 및 동작 확인
- [ ] AgentMonitor 가동, 메트릭 기록
- [ ] **영어 슬라이드 골격** 시작 (목차만이라도)

### Week 13 — 실험 + 보고서 + 드라이런

**Done의 정의**:
- [ ] `bench/run_all.sh` 실행 완료, `out/REPORT.md` 생성
- [ ] 5가지 실험 모두 목표 달성 또는 미달 시 사유 분석
- [ ] **Technical Report** 작성 완료
- [ ] **Development Process Document** (`PROCESS.md`) 작성
- [ ] **영어 슬라이드** 1차 완성
- [ ] 드라이런 1회 (15분 발표 시간 측정)

### Week 14 — 최종 발표

**Done의 정의**:
- [ ] 슬라이드 최종본, 데모 GIF/비디오
- [ ] 발표 (Professor 15% + Peer review 15%)
- [ ] 저장소 README 최종 정리, 데모 스크린샷 추가

---

## 8. 산출물 매핑 (가이드 §5)

| # | 가이드 §5 산출물 | 본 프로젝트 위치 |
|---|---|---|
| 1 | **Application** | xv6 소스 수정본 (`xv6-src/`), Proxy Daemon (`host/`), `bench/`, 데모 GIF (`docs/demo.gif`) |
| 2 | **Technical Report** | 본 문서 + `out/REPORT.md` (실험 결과) |
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
