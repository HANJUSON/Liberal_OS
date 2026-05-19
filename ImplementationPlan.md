# 구현 계획서 — LLM 멀티에이전트 OS

> 본 문서는 [GuideLine.md](GuideLine.md)의 요구사항을 충족하면서 [README.md](README.md)에 정리된 OS 메커니즘 카탈로그를 **실행 가능한 단일 계획**으로 좁힌 결정 문서입니다.
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

**Liberal_OS**팀은 여러 LLM 에이전트를 **OS 레벨의 격리·스케줄링·자원 관리 메커니즘**으로 오케스트레이션하여 **대량 로그 트리아지**를 병렬 처리하는 시스템을 고안합니다. 각 에이전트(parser / classifier / root-cause / fix-suggester)는 독립 프로세스로 실행되며, 오케스트레이터는 `multiprocessing.Queue`로 IPC를 수행하고, `cgroups`/`setpriority`로 에이전트별 자원 한도와 우선순위를 부여한다. 본 프로젝트는 LLM API를 **호출하는 것이 아니라 자원으로 취급해 OS가 관리**하는 데 초점이 있으며, GIL 우회 speedup·장애 격리·자원 한도 준수를 정량 측정한다.

### 1.2 선택한 방향: **A (OS for LLM)**

가이드라인 §2의 방향 A — "LLM을 호스팅·서빙·오케스트레이션하는 OS, 런타임 계층, 또는 에이전트 플랫폼"에 해당. 구체적으로는 **"여러 개의 동시 LLM 프로세스(에이전트)를 CPU/메모리/도구 쿼터를 공정하게 할당하며 관리하는 미니 OS 또는 유저스페이스 슈퍼바이저"** 범주.

### 1.3 README.md와의 관계

| 문서 | 역할 |
|---|---|
| [README.md](README.md) | OS 메커니즘 **카탈로그**. 4가지 장점, 5가지 구현 방법, 평가 코드, AgentMonitor 클래스 정리. 보고서의 기술 부록으로 재활용. |
| **본 문서 (ImplementationPlan.md)** | **결정·실행 레이어**. "무엇을·어떻게·언제·어떤 기준으로" 만들 것인지 명시. |

README.md는 수정하지 않고 그대로 보존하되, 본 문서가 README.md의 어느 절을 채택하고 어느 절을 부록으로 보낼지 §4.4에서 매핑한다.

---

## 2. 킬러 시나리오 — 로그 트리아지 파이프라인

### 2.1 채택 이유

가이드라인 §2의 **필수 제약**은 "OS 컴포넌트가 단지 'LLM이 그 위에 돌아간다'는 정도로는 안 된다"고 명시한다. 로그 트리아지는 다음 이유로 OS 결정이 **substantive**하게 들어간다.

- **대량 입력**: 수천~수만 줄 로그를 다루므로 병렬화 가치(speedup 2x+)가 자연스럽게 정당화됨
- **격리 필요성**: 로그 한 줄 파싱 실패가 전체 파이프라인을 죽이면 안 됨 → **프로세스 격리**가 단순 장식이 아니라 요구사항
- **자원 한도**: 분류기가 메모리를 폭주시킬 때 다른 에이전트는 살아남아야 함 → **cgroups**가 명확한 가치
- **우선순위**: critical 등급 로그는 먼저 처리해야 함 → **nice/setpriority**가 기능적 의미를 가짐

### 2.2 에이전트 구성

```
[Raw Log Stream]
       │
       ▼
┌────────────────┐
│ parser         │  로그 라인 → 구조화된 dict
│  (4 workers)   │  (Upstage Solar Pro 3)
└────────┬───────┘
         │
         ▼
┌────────────────┐
│ classifier     │  레벨/카테고리 분류
│  (4 workers)   │  (INFO/WARN/ERROR/CRITICAL)
└────────┬───────┘
         │
         ▼
┌────────────────┐
│ root-cause     │  ERROR/CRITICAL만 진단
│  (2 workers)   │
└────────┬───────┘
         │
         ▼
┌────────────────┐
│ fix-suggester  │  수정 제안 생성
│  (2 workers)   │
└────────┬───────┘
         │
         ▼
   [Triage Report]
```

총 N=12 에이전트 프로세스(시연용 기본값). 평가 실험에서는 N=4 (역할당 1개)로 통제.

### 2.3 입력/출력 정의

- **입력**: `samples/nginx-access-1000.log`, `samples/kernel-dmesg.log` 등 (각 1,000~5,000줄)
- **출력**: `out/report.json` — 구조화된 트리아지 결과
- **시연 시나리오**: 콘솔에서 `python -m liberal_os triage samples/kernel-dmesg.log` 실행 → AgentMonitor가 실시간 메트릭 표시 → 30초 내 리포트 생성

### 2.4 대안 검토 (채택 안 함)

- **리서치 보고서 생성**: 시연 직관적이나, "왜 OS 격리가 필요한가"의 정당화가 약함. 검색 실패가 보고서를 죽이지 않으므로 격리의 가치 희박.
- **코드 리뷰 파이프라인**: 샌드박스 격리 데모는 강력하나, LLM이 생성한 코드를 실제로 안전 실행하는 `seccomp`/`namespaces` 구현이 14주에 무리.

---

## 3. 시스템 아키텍처

### 3.1 블록 다이어그램

```
                       ┌──────────────────────────────┐
                       │   Orchestrator (main proc)   │
                       │   - spawn agents             │
                       │   - setpriority(nice=-10)    │
                       │   - signal handler           │
                       └────┬───────────────────┬─────┘
                            │                   │
                       fork │                   │ monitor
                            ▼                   ▼
        ┌──────────────────────────┐   ┌────────────────────┐
        │  multiprocessing.Queue   │   │  AgentMonitor      │
        │  (task in / result out)  │   │  - psutil sampling │
        └─┬────────┬────────┬──────┘   │  - cgroups stats   │
          │        │        │          │  - log to file     │
          ▼        ▼        ▼          └────────────────────┘
       ┌────────────┐ ┌────────────┐ ┌────────────┐
       │A1: parser  │ │A2: class.  │ │A3: root.   │  ... Process()
       │ client₁    │ │ client₂    │ │ client₃    │  ← 프로세스별
       │ httpx pool │ │ httpx pool │ │ httpx pool │     OpenAI 인스턴스
       │ TLS sess.  │ │ TLS sess.  │ │ TLS sess.  │     (fork-safe X)
       └─────┬──────┘ └─────┬──────┘ └─────┬──────┘
             │              │              │
             └──────┬───────┴──────┬───────┘
                    │              │
                    ▼              ▼
             ┌─────────────────────────────┐
             │  MPSemaphore(8)             │  동시 호출 상한
             │  (multiprocessing.Semaphore)│  (세션 수 ≠ 동시 호출 수)
             └──────────────┬──────────────┘
                            │
                            ▼
                  api.upstage.ai (Solar Pro 3)
                            │
                            ▼
                  ┌────────────────────┐
                  │  LLM Response Cache│
                  │  (.cache/llm/*.pkl)│  ← 파일시스템으로 공유
                  └────────────────────┘
```

> 각 에이전트 프로세스는 **독립된 `OpenAI` 클라이언트(= 자체 `httpx` 커넥션 풀 + TLS 세션)** 를 가진다. `httpx`는 fork-safe하지 않으므로 부모에서 생성한 클라이언트를 자식에 전달할 수 없으며, **자식 프로세스 내부에서 생성**한다. 이는 장애 격리가 네트워크 세션까지 확장된다는 의미다 (한 에이전트의 커넥션 깨짐이 다른 에이전트에 영향 없음). 동시 호출 수는 별도로 `multiprocessing.Semaphore`로 상한을 둔다 (§5.3).

### 3.2 OS 개념 매핑표

가이드라인 §2 "OS 개념이 substantive하게 포함" 요구를 충족하기 위해 다음 7개 OS 개념을 **직접 설계·구현**한다.

| # | 컴포넌트 | OS 개념 | 구현 수단 | README.md 참조 |
|---|---|---|---|---|
| 1 | Orchestrator | **프로세스 관리** | `multiprocessing.Process`, `fork`/`exec` | §1-2, §2-2 |
| 2 | Agent 통신 | **IPC** | `multiprocessing.Queue` | §2-2 |
| 3 | 공유 컨텍스트 | **동기화** | `Manager().dict()` + `Lock` | §1-2, §2-3 |
| 4 | 에이전트별 자원 한도 | **가상 메모리 / cgroups** | cgroups v2 (`memory.max`, `cpu.weight`) | §1-3, §3-3 |
| 5 | 우선순위 제어 | **스케줄링** | `os.setpriority()`, `renice` | §1-4, §3-4 |
| 6 | 장애 격리 | **시그널 / 프로세스 격리** | `SIGKILL` 핸들링, 자식 재시작 | §1-2, §3-2 |
| 7 | 모니터링 | **시스템 콜 / /proc** | `psutil`, `AgentMonitor` | §4-1 |
| **8** | **API 세션 격리 + 동시성 상한** | **파일 디스크립터 / 네트워크 자원 / 세마포어** | 프로세스별 독립 `httpx` 커넥션 풀, `multiprocessing.Semaphore(8)` | (해당 없음, 신규 결정) |

비교군(threading) 구현에서는 #1, #2가 `threading.Thread` + `queue.Queue`로 대체되고, **#8은 단일 `OpenAI` 클라이언트 공유 + `threading.Semaphore(8)`** 로 단순화된다 (`httpx.Client`는 thread-safe). 이 비대칭 자체가 §6.1의 "스레드 vs 프로세스" 비교 실험의 흥미로운 변수다 (TLS handshake 비용·메모리·커넥션 풀 분리 여부).

### 3.3 시퀀스 다이어그램

```
User      Orchestrator   Q1(parser-in)  ParserAgent   Q2(class-in)  ClassAgent   Cache    Upstage
 │             │              │              │             │             │          │         │
 │─triage────▶│              │              │             │             │          │         │
 │             │─spawn──────▶│              │             │             │          │         │
 │             │              │              │             │             │          │         │
 │             │─task─────────▶│              │             │             │          │         │
 │             │              │              │─get─────────│             │          │         │
 │             │              │              │─lookup──────────────────▶│          │         │
 │             │              │              │            (miss)        │          │         │
 │             │              │              │─call(prompt)──────────────────────▶│         │
 │             │              │              │◀──response──────────────────────────│         │
 │             │              │              │─store──────────────────▶│          │         │
 │             │              │              │─put─────────▶│             │          │         │
 │             │              │              │              │─get────────▶│         │         │
 │             │              │              │              │            ...       │         │
 │◀────report─│              │              │              │             │         │         │
```

---

## 4. 구현 방법 결정

### 4.1 메인: `multiprocessing` + `Queue`

**채택 이유**:
- 프로세스 격리·자원 제어·시그널 등 **OS 결정의 표면적이 가장 큼**
- GIL 우회의 정량적 증명이 가능 (가이드 §5의 평가 항목)
- `cgroups`, `setpriority`, `SIGKILL` 모두 자연스럽게 동작

**참조**: [README.md §2-2](README.md#2-2-멀티프로세스-기반) (단, Upstage SDK로 교체)

### 4.2 비교군: `threading` + `Lock`

**채택 이유**:
- LLM API 호출은 **I/O 바운드**이므로 스레드가 실제로 더 빠를 가능성 → 흥미로운 정량 결과 보장
- 같은 워크로드에서 메모리 사용량/격리 효과 차이를 명확히 보여줌
- README.md §2-3을 그대로 재활용 가능

**참조**: [README.md §2-3](README.md#2-3-멀티스레드-기반)

### 4.3 채택하지 않은 방법

| 방법 | 채택 안 한 이유 | 보고서 처리 |
|---|---|---|
| Unix 파이프 | 순차 실행이라 병렬성 측정 불가, OS 결정의 폭 좁음 | 부록 §A.1에 1단락 |
| 소켓 / gRPC | 단일 머신 데모이므로 분산 가치 약함, 구현 면적 큼 | 부록 §A.2에 1단락 |
| 공유 파일시스템 | 비동기성과 일관성 모두 약함, 프로토타입 수준 | 부록 §A.3에 1단락 |

### 4.4 README.md 매핑

| README.md 절 | 처리 |
|---|---|
| §1-1 ~ §1-4 (장점 4가지) | **본 문서 §3.2 매핑표로 흡수**, Technical Report에서 인용 |
| §2-1 (아키텍처 비교표) | 본 문서 §4.1~4.3 결정의 근거 자료로 인용 |
| §2-2 (멀티프로세스) | **메인 구현으로 채택**, Upstage SDK로 코드 교체 |
| §2-3 (멀티스레드) | **비교군 구현으로 채택**, Upstage SDK로 코드 교체 |
| §2-4 (Unix 파이프) | 부록으로 격하 |
| §2-5 (소켓) | 부록으로 격하 |
| §3-1 ~ §3-4 (평가 코드) | **본 문서 §6.3 측정 자동화 스크립트의 기반** |
| §4-1 (AgentMonitor) | **그대로 채택**, 본 문서 §6의 측정 인프라 |
| §4-2 (핵심 지표 요약) | **본 문서 §6.1로 확장** (반복 횟수·통제 변수 추가) |
| §4-3 (선택 가이드) | 본 문서 §4 결정의 근거 자료 |

---

## 5. LLM 백엔드 — Upstage Solar Pro 3

가이드라인 §4 강제 사항. README.md의 `anthropic` SDK 코드는 모두 다음 패턴으로 교체된다.

### 5.1 코드 패턴 (anthropic → OpenAI 호환)

```python
# 변경 전 (README.md §2-2)
import anthropic
client = anthropic.Anthropic()
response = client.messages.create(
    model='claude-sonnet-4-20250514',
    max_tokens=1000,
    system=system_prompt,
    messages=[{'role': 'user', 'content': task}]
)
text = response.content[0].text

# 변경 후 (Upstage Solar Pro 3, OpenAI 호환)
import os
from openai import OpenAI

client = OpenAI(
    api_key=os.environ['UPSTAGE_API_KEY'],
    base_url='https://api.upstage.ai/v1',
)
response = client.chat.completions.create(
    model='solar-pro',
    max_tokens=1000,
    messages=[
        {'role': 'system', 'content': system_prompt},
        {'role': 'user', 'content': task},
    ],
)
text = response.choices[0].message.content
```

### 5.2 API 키 관리

```bash
# .env (git에 절대 커밋 금지)
UPSTAGE_API_KEY=up-xxxxxxxxxxxxxxxxxxxx

# .gitignore
.env
.env.*
*.key
__pycache__/
out/
.cache/llm/
```

코드는 `python-dotenv`로 로드:

```python
from dotenv import load_dotenv
load_dotenv()
```

### 5.3 Rate Limit 대응 — 지수 백오프 + 로컬 큐

```python
import time, random

def call_llm_with_backoff(client, **kwargs):
    for attempt in range(6):
        try:
            return client.chat.completions.create(**kwargs)
        except Exception as e:
            if 'rate' not in str(e).lower() and attempt > 2:
                raise
            sleep = (2 ** attempt) + random.random()
            time.sleep(sleep)
    raise RuntimeError('rate limit: exhausted retries')
```

동시 호출 상한은 다음과 같이 backend별로 다르게 적용된다.

```python
# 메인(multiprocessing): 프로세스 간 공유 세마포어
from multiprocessing import Semaphore as MPSemaphore

api_sem = MPSemaphore(8)  # 오케스트레이터에서 생성, fork로 자식 상속

def agent_process(name, in_q, out_q, api_sem):
    client = OpenAI(                    # ★ 자식 프로세스 안에서 생성
        api_key=os.environ['UPSTAGE_API_KEY'],
        base_url='https://api.upstage.ai/v1',
    )
    while True:
        task = in_q.get()
        if task == 'STOP': break
        with api_sem:                   # 12개 프로세스 중 최대 8개만 동시 호출
            response = cached_llm_call(client, 'solar-pro', SYS_PROMPT, task)
        out_q.put(response)

# 비교군(threading): 일반 세마포어 + 단일 클라이언트 공유
import threading
client = OpenAI(...)                    # 메인 스레드에서 1개 (httpx는 thread-safe)
api_sem = threading.Semaphore(8)
```

> **세션 수 ≠ 동시 호출 수**. 멀티프로세스에서는 12개 에이전트 = 12개 클라이언트 = 12개 커넥션 풀이지만, 한 시점에 Upstage를 두드리는 호출은 최대 8개로 제한된다.

### 5.4 비결정성 통제 — LLM 응답 캐시

평가 측정 시 LLM 응답이 매번 달라지면 speedup·격리율 측정이 오염된다. 입력 해시로 키를 만들어 디스크에 캐싱한다.

```python
import hashlib, pickle, pathlib

CACHE_DIR = pathlib.Path('.cache/llm')
CACHE_DIR.mkdir(parents=True, exist_ok=True)

def cached_llm_call(client, model, system, user):
    key = hashlib.sha256(
        f'{model}|{system}|{user}'.encode()
    ).hexdigest()
    cache_file = CACHE_DIR / f'{key}.pkl'
    if cache_file.exists():
        return pickle.loads(cache_file.read_bytes())
    response = call_llm_with_backoff(
        client,
        model=model,
        messages=[
            {'role': 'system', 'content': system},
            {'role': 'user', 'content': user},
        ],
    )
    cache_file.write_bytes(pickle.dumps(response))
    return response
```

**평가 모드 옵션**:
- `--mode live`: 실 API 호출 (시연용)
- `--mode replay`: 캐시만 사용 (평가 측정용, 재현성 보장)
- `--mode mock`: `time.sleep(uniform(0.5, 2.0))` + 더미 응답 (CI/단위 테스트용)

### 5.5 README.md anthropic 코드 교체 diff 요약

| README.md 위치 | 교체 작업 |
|---|---|
| §2-2 `import anthropic` | → `from openai import OpenAI` |
| §2-2 `client = anthropic.Anthropic()` | → Upstage `base_url` 지정 |
| §2-2 `client.messages.create(model='claude-sonnet-4-20250514', ...)` | → `client.chat.completions.create(model='solar-pro', ...)` |
| §2-2 `response.content[0].text` | → `response.choices[0].message.content` |
| §2-2 `system=system_prompt` 인자 | → `messages` 안의 `role: 'system'` 메시지 |
| §2-4 `# researcher.py`의 `client.messages.create(...)` | → 동일 패턴 적용 |

→ 실제 구현 코드는 별도의 `src/` 폴더에서 Upstage 패턴으로 처음부터 작성. README.md는 기술 카탈로그로 보존.

---

## 6. 평가 계획

### 6.1 평가 지표 (README §4-2 확장)

README.md의 목표 기준(2x 이상, 100% 생존, 70%+ 코어 활용)을 **그대로 보존**하면서, 측정 절차와 반복 횟수를 추가한다.

| 지표 | 측정 방법 | 목표 | 반복 |
|---|---|---|---|
| **병렬 처리 효과 (speedup)** | 순차 wall-clock / 병렬 wall-clock | **2x 이상** | 5회 평균 ± stdev |
| **장애 격리** | 자식 SIGKILL 후 나머지 생존 비율 | **100%** | 10회 (각 에이전트 1회씩) |
| **메모리 한도 준수** | cgroups `memory.max` 초과 시 OOM 격리, 다른 에이전트 영향 0 | 한도 내 유지 | 3회 |
| **CPU 우선순위 효과** | `nice -10` vs `nice +15` 두 에이전트의 CPU 점유 시간 비교 | 비대칭 분배 확인 | 1회 (60초) |
| **코어 활용률** | `htop` / `psutil.cpu_percent(percpu=True)` 평균 | **70%+** | 측정 60초 |
| **스레드 vs 프로세스** | 동일 task 셋에서 multiprocessing vs threading wall-clock | 정량 보고 | 5회 평균 |

### 6.2 통제 변수

평가의 재현성 보장:

- **LLM 응답**: `--mode replay` (캐시된 응답만 사용)
- **입력 task 셋**: `samples/eval/triage-100.jsonl` (100개 고정)
- **에이전트 수**: N=4 (역할당 1개)
- **하드웨어**: 동일 머신, 백그라운드 프로세스 최소화
- **반복**: 각 실험 5회 이상, 평균과 표준편차 보고

### 6.3 측정 자동화 (`bench/run_all.sh`)

```bash
#!/usr/bin/env bash
set -euo pipefail

mkdir -p out/bench

echo "[1/6] sequential baseline..."
for i in 1 2 3 4 5; do
  python -m liberal_os.bench sequential \
    --input samples/eval/triage-100.jsonl \
    --mode replay > out/bench/seq_$i.json
done

echo "[2/6] multiprocessing parallel..."
for i in 1 2 3 4 5; do
  python -m liberal_os.bench parallel --backend mp \
    --workers 4 --mode replay > out/bench/mp_$i.json
done

echo "[3/6] threading parallel..."
for i in 1 2 3 4 5; do
  python -m liberal_os.bench parallel --backend thread \
    --workers 4 --mode replay > out/bench/th_$i.json
done

echo "[4/6] fault isolation (SIGKILL)..."
python -m liberal_os.bench fault_isolation \
  --iterations 10 > out/bench/fault.json

echo "[5/6] cgroups memory limit..."
sudo python -m liberal_os.bench cgroups_memory \
  --limit 512M --iterations 3 > out/bench/cgroups.json

echo "[6/6] CPU priority..."
python -m liberal_os.bench priority \
  --high -10 --low 15 --duration 60 > out/bench/prio.json

python -m liberal_os.bench report out/bench/ > out/REPORT.md
echo "Done: out/REPORT.md"
```

리포트는 `out/REPORT.md`에 표 + matplotlib 그래프 경로로 자동 생성.

---

## 7. 일정 — Week 10~14

### Week 10 — 시스템 스케치

**Done의 정의**:
- [ ] 본 문서를 팀원 전원이 리뷰하고 §2(시나리오) 최종 확정
- [ ] 블록 다이어그램(§3.1)을 draw.io로 작성, 저장소에 PNG 커밋
- [ ] 시퀀스 다이어그램(§3.3)을 mermaid로 변환
- [ ] **공식 제안서 1문단**(§1.1) 강사 제출
- [ ] Upstage API 키 수령 및 `.env` 셋업, `hello-upstage.py` 동작 확인
- [ ] 저장소 README.md를 가이드 §3 형식(요약·방향·기술스택·셋업·실행·데모 자리표시)으로 재구성
- [ ] **역할 분담 확정** (본 문서 §10)

### Week 11 — MVP

**Done의 정의**:
- [ ] `liberal_os/orchestrator.py`: `multiprocessing.Process` 3개 spawn → Queue로 task 분배
- [ ] `liberal_os/agents/parser.py`, `classifier.py`: Upstage API end-to-end 호출
- [ ] LLM 응답 캐시(§5.4) 동작
- [ ] 1개 시연용 로그(`samples/nginx-access-100.log`)가 처음부터 끝까지 처리됨
- [ ] 자식 1개 SIGKILL → 부모 생존 확인 테스트 1개 통과
- [ ] CI 통과 (`--mode mock`)

### Week 12 — 통합 + 자원 제어

**Done의 정의**:
- [ ] 4개 역할 에이전트 모두 동작 (parser/classifier/root-cause/fix-suggester)
- [ ] `multiprocessing.Manager().dict()` + `Lock`로 공유 컨텍스트
- [ ] `os.setpriority()` 적용, `nice` 값이 `ps -o ni`로 확인됨
- [ ] cgroups v2 스크립트(`scripts/cgroups_setup.sh`)로 에이전트 그룹 메모리 한도 적용
- [ ] AgentMonitor 가동, 메트릭이 `out/metrics-*.jsonl`에 기록됨
- [ ] threading 비교군 구현 (`--backend thread`)
- [ ] **영어 슬라이드 골격** 시작 (목차만이라도)

### Week 13 — 실험 + 보고서 + 드라이런

**Done의 정의**:
- [ ] `bench/run_all.sh` 실행 완료, `out/REPORT.md` 생성
- [ ] 6가지 실험 모두 목표 달성 또는 미달 시 사유 분석
- [ ] **Technical Report** 작성 완료 (본 문서 + 실험 결과 + README.md 부록 통합)
- [ ] **Development Process Document** (`PROCESS.md`) 작성 (회의록, 주차별 진행, 이슈 해결)
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
| 1 | **Application** | 저장소 루트 코드 (`src/liberal_os/`), `samples/`, `bench/`, 데모 GIF (`docs/demo.gif`) |
| 2 | **Technical Report** | 본 문서 + [README.md](README.md) (부록) + `out/REPORT.md` (실험 결과) |
| 3 | **Development Process Document** | `PROCESS.md` (Week 12에 생성), `docs/meetings/` 폴더 |
| 4 | **Presentation Slides** (영어) | `slides/final.pptx` (Week 12부터 작성, Week 14 최종) |

---

## 9. 위험과 대응

| 위험 | 신호 | 대응 |
|---|---|---|
| **"얇은 래퍼" 오해** | 보고서가 "API를 multiprocessing으로 호출한 것"으로 읽힘 | Technical Report를 **OS 메커니즘 중심 서술**로 작성. LLM 호출은 §5에 격리하고, §3 OS 매핑표를 보고서 첫 장에 배치. "우리가 만든 OS 컴포넌트" 7개를 명시. |
| **Upstage rate limit** | 동시 호출 시 429 응답 | §5.3 백오프 + §5.4 캐시로 대부분 흡수. 동시 호출 상한 `Semaphore(8)`로 제한. |
| **LLM 비결정성** | speedup 측정값이 매번 달라짐 | `--mode replay`로 캐시된 응답만 사용. 실 API 호출은 시연 시에만. |
| **시연 매력 부족** | 발표 데모가 콘솔 텍스트뿐 | AgentMonitor 출력을 `rich`로 텍스트 대시보드화 (htop 스타일). 시간 남으면 작은 web UI. |
| **영어 발표 지연** | Week 13에 슬라이드가 없음 | Week 12부터 모든 코드 주석과 보고서 핵심 문장을 영어로 작성. 슬라이드는 한글 메모 → 영어 변환이 아니라 처음부터 영어. |
| **cgroups v2 호환성** | 머신마다 cgroups 경로 다름 | `scripts/cgroups_setup.sh`에서 cgroups v1/v2 감지 분기. 실패 시 `ulimit` fallback. |
| **시나리오 정당화 실패** | 로그 트리아지가 너무 단순해 LLM 가치가 약해 보임 | 입력 로그를 **다국어·비정형**으로 선택 (한글 시스템 로그, 잡음 섞인 dmesg) → 룰 기반 파서로는 어렵게 만듦. |

---

## 10. 역할 분담

> **TBD — Week 10 회의에서 확정**

가이드라인 §1에 따라 4인 팀(또는 예외적 3인). 권장 분담 (Week 10 회의에서 조정):

| 역할 | 책임 영역 | 주요 산출물 |
|---|---|---|
| **팀 리더 / 오케스트레이션** | 단일 연락 창구, `orchestrator.py`, 일정 관리 | 일정표, 회의록, 통합 빌드 |
| **OS 시스템** | `multiprocessing` 구조, cgroups, setpriority, 시그널 핸들링 | §3.2 매핑표의 1, 4, 5, 6번 |
| **에이전트 / LLM** | Upstage 통합, 4개 에이전트 구현, 캐시 | §5 전체, 에이전트 모듈 |
| **평가 / 모니터링** | AgentMonitor, `bench/run_all.sh`, 영어 슬라이드 | §6 전체, 실험 결과, slides/ |

3인 팀 예외 적용 시: 평가/모니터링을 OS 시스템과 합침.

---

## 부록 A — 채택하지 않은 구현 방법 (참조용)

§4.3에서 배제된 3가지 방법은 [README.md §2-4, §2-5](README.md#2-4-unix-파이프-기반)에 보존되어 있으며, Technical Report 작성 시 1단락씩 인용한다.

- **A.1 Unix 파이프**: 순차 실행이라 본 프로젝트의 핵심 가치(병렬 speedup)와 부합하지 않음.
- **A.2 소켓 / gRPC**: 단일 머신 데모에서는 IPC 오버헤드만 증가. 분산 배포로 확장 시 채택 가치 있음 (향후 작업).
- **A.3 공유 파일시스템**: 일관성 보장이 약해 평가의 신뢰성 저하. 프로토타입 수준에서만 유의미.

---

## 부록 B — 셀프 체크리스트 (가이드 §3 README 요구사항)

본 프로젝트 저장소의 최종 README.md에 다음이 모두 있는지 Week 14 전 점검:

- [ ] 한 문단 프로젝트 요약 + 방향 (A)
- [ ] 기술 스택 개요
- [ ] 셋업 안내 (의존성, `.env`, Upstage API 키 발급 방법)
- [ ] 실행 방법 (`python -m liberal_os triage ...`)
- [ ] 데모 스크린샷 / GIF
- [ ] 저장소 public 설정 확인
- [ ] API 키가 git에 커밋되지 않았는지 확인 (`git log -p | grep -i 'up-'`)
