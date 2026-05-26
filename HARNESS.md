# HARNESS.md — Liberal_OS 하네스 엔지니어링 설계서

> 이 문서는 **사람(팀원·평가자)이 읽는** 설계서다. Claude Code의 자율 작동을 가능하게 하는 하네스(harness)의 구조, 설계 이유, 운영 방법을 설명한다.
>
> 본 문서는 `Development Process Document` (가이드 §5 산출물 #3)의 핵심 자료가 된다.

---

## 목차

1. [하네스란 무엇인가](#1-하네스란-무엇인가)
2. [왜 xv6 프로젝트에 하네스가 필수인가](#2-왜-xv6-프로젝트에-하네스가-필수인가)
3. [하네스의 5가지 기둥](#3-하네스의-5가지-기둥)
4. [구성요소 카탈로그](#4-구성요소-카탈로그)
5. [Claude Max 20x 운용 전략](#5-claude-max-20x-운용-전략)
6. [4인 팀 git worktree 배치](#6-4인-팀-git-worktree-배치)
7. [일일 운영 매뉴얼](#7-일일-운영-매뉴얼)
8. [실패 시나리오와 대응](#8-실패-시나리오와-대응)
9. [한계와 정직한 평가](#9-한계와-정직한-평가)

---

## 1. 하네스란 무엇인가

### 1.1 정의

**하네스(harness)** 는 Claude Code가 **사람의 매 분 개입 없이 코드를 작성·검증·반복할 수 있도록 만들어주는 외부 골격**이다. 마차에 말을 매는 마구(harness)에서 유래한 표현으로, AI 코딩 에이전트 운영 분야에서 자율 작동 인프라를 가리키는 일반 용어가 되었다.

### 1.2 구성

하네스는 단일 도구가 아니라 **여러 파일·스크립트·규약의 집합**이다. Liberal_OS 프로젝트의 하네스는 다음을 포함한다:

| 종류 | 파일 |
|---|---|
| 운영 매뉴얼 | `CLAUDE.md` |
| 작업 큐 | `MASTER_PLAN.md` (Part II) |
| 막힘 보고 | `BLOCKED.md` |
| 도구 권한 | `.claude/settings.json` |
| 자동 검증 | `tests/autotest.sh`, `tests/regression.sh` |
| 디버깅 인프라 | `tests/gdb_on_panic.sh`, 커널 PANIC_DUMP 매크로 |
| 결과 요약 | `bench/summarize.py`, `bench/report.py` |
| 작업 격리 | `git worktree` 운영 규약 |

---

## 2. 왜 xv6 프로젝트에 하네스가 필수인가

### 2.1 일반 프로젝트와의 차이

일반적인 Python/TypeScript 프로젝트는 Claude Code가 거의 즉시 자율 작동에 들어갈 수 있다. 코드 수정 → `pytest` → 1초 안에 결과. 한 시간에 수십~수백 번 반복 가능.

xv6 프로젝트는 다르다:

| 항목 | 일반 프로젝트 | Liberal_OS (xv6) |
|---|---|---|
| 한 검증 사이클 | 1~10초 | **2~5분** (build + boot + test) |
| 실패 정보 풍부함 | stack trace + assertion message | "panic: kerneltrap" 한 줄 |
| 실패 시 자동 복구 | 가능 (테스트 격리됨) | xv6 자체가 죽으면 다음 사이클 불가 |
| 작업 단위 격리 | 함수 단위 | 파일 + 빌드 시스템 단위 |
| 사람 개입 빈도 (하네스 없을 때) | 1~2시간마다 | **10~20분마다** |

하네스 없이 xv6 프로젝트에 Claude Code를 풀면, 사람이 매 20분마다 babysitting하게 된다. 14주 프로젝트에서 이는 지속 불가능하다.

### 2.2 하네스의 효과

하네스를 제대로 깔면 다음이 가능해진다:

- **사람 개입 빈도**: 20분/회 → 2~4시간/회 (10배 감소)
- **하루 자율 작동 시간**: 2~3시간 → 8~10시간 (3배 증가)
- **병렬 작업 스트림**: 1개 → 3~5개 (git worktree 활용)
- **회귀 사고**: 빈번 → 거의 없음 (regression gate)

이 효과의 누적이 14주 일정의 실현 가능성을 좌우한다.

---

## 3. 하네스의 5가지 기둥

자율 작동 하네스를 떠받치는 5가지 기둥. 하나라도 약하면 전체가 무너진다.

### 기둥 ① 명확한 다음 작업

CC가 자율적으로 돌려면 "지금 뭘 해야 하나"를 코드 외부에서 알 수 있어야 한다. 매번 사람이 지시하면 자율이 아니다.

**구현**: `MASTER_PLAN.md` (Part II) — 의존성 그래프와 검증 명령이 명시된 작업 큐.

### 기둥 ② 빠른 검증 루프

"내가 한 게 맞는지" 60초 안에 알 수 있어야 한다. 그래야 CC가 반복 실패를 빨리 탈출한다.

**구현**: `make autotest` — 헤드리스 QEMU에서 xv6 자동 부팅 + smoketest + grep 판정.

### 기둥 ③ 풍부한 실패 신호

실패 시 원인을 추측이 아니라 데이터로 알아야 한다. 추측 디버깅은 토큰을 태우면서 진전이 없다.

**구현**:
- 커널 `panic()` 후킹으로 `[PANIC_DUMP_BEGIN]...[PANIC_DUMP_END]` 자동 출력
- 실패 시 `out/last-fail.log`, `out/last-fail.diff` 자동 저장
- GDB 자동 첨부 스크립트 (`tests/gdb_on_panic.sh`)

### 기둥 ④ 작업 격리

한 작업의 실패가 다른 작업을 막지 않아야 한다. 그래야 병렬화가 가능하다.

**구현**: `git worktree` — 4~5개 작업 트리, 각자 독립 브랜치.

### 기둥 ⑤ 안전망

잘못된 자율 작업을 되돌릴 수 있어야 한다. 그래야 사람이 자율 작동을 안심하고 허용한다.

**구현**:
- 자동 체크포인트 커밋 (검증 통과 시마다)
- HUMAN GATE (위험 작업 사전 승인)
- 일일 자동 백업 (`git tag daily-YYYYMMDD`)
- 회귀 게이트 (`tests/regression.sh`)

---

## 4. 구성요소 카탈로그

### 4.1 `CLAUDE.md` — 운영 매뉴얼

Claude Code가 모든 세션 시작 시 자동으로 읽는 파일. 내용:
- 절대 규칙 8개 (예: anthropic SDK 금지, push 금지)
- 작업 흐름 (Work Loop) 규약
- 디렉토리 규약
- 코드 스타일
- 도구 사용 규약
- 디버깅 절차
- 컨텍스트 절약 가이드
- 모델 선택 (Opus vs Sonnet)
- 막힘 처리 규약
- 자기 검증 체크리스트

**원칙**: 한 번 작성 후 거의 수정하지 않는다. 수정은 팀 회의에서 결정.

### 4.2 `MASTER_PLAN.md` (Part II) — 작업 큐

7개 Phase, 약 50~60개 작업. 각 작업은:
- ID (T-NN)
- 의존성 (`depends:`)
- 건드릴 파일 (`files:`)
- 검증 명령 (`verify:`)
- 예상 소요 (`estimate:`)
- 담당 (`assignee:`)
- HUMAN GATE 여부 (🔴)

**살아있는 문서**. CC가 작업 시작/종료 시 상태를 직접 갱신.

### 4.3 `.claude/settings.json` — 도구 권한

```json
{
  "allowed_tools": [
    "Bash(make:*)",
    "Bash(git status)",
    "Bash(git diff:*)",
    "Bash(git add:*)",
    "Bash(git commit:*)",
    "Bash(grep:*)",
    "Bash(find:*)",
    "Bash(cat:*)",
    "Bash(head:*)",
    "Bash(tail:*)",
    "Bash(jq:*)",
    "Bash(python host/proxy_daemon.py:*)",
    "Edit",
    "Write"
  ],
  "disallowed_tools": [
    "Bash(sudo:*)",
    "Bash(rm -rf:*)",
    "Bash(git push:*)",
    "Bash(git reset --hard:*)"
  ]
}
```

승인 prompt를 줄여 자율 작동을 가능하게 만드는 핵심 파일.

### 4.4 `BLOCKED.md` — 막힘 보고

CC가 막혔을 때 자동으로 기록하는 파일. 사람이 매일 아침 확인.

형식:
```markdown
## T-NN: <작업명> — BLOCKED at <ISO timestamp>

**증상**: <한 문장>
**시도한 것**: <목록>
**가설**: <원인 추정>
**필요한 사람 결정**: <구체적 질문>
```

### 4.5 `tests/autotest.sh` — 60초 자동 검증

```
1. pkill -f qemu-system-riscv64       # 좀비 청소
2. make clean && make                  # stale 캐시 방지
3. timeout 60 qemu ... < smoke.in > log
4. grep -q "SMOKE_TEST_PASS" log
5. PASS / FAIL 출력 + last-fail.log 저장
```

매 커밋 전 통과 필수.

### 4.6 `tests/regression.sh` — 회귀 게이트

```
1. autotest 통과 (기본 부팅 + smoketest)
2. 기본 셸 명령 (ls, cat, fork) 정상
3. Proxy Daemon hello 응답
4. 마지막 완료된 Phase의 시나리오 1개
```

CC가 회귀를 만들면 커밋 자체가 차단된다.

### 4.7 커널 디버깅 인프라

#### PANIC_DUMP
xv6 `panic()` 함수를 수정하여 모든 active proc의 상태를 dump:
```c
[PANIC_DUMP_BEGIN]
pid=1 state=SLEEPING role=parser prio=3
pid=2 state=RUNNABLE role=classifier prio=2
...
[PANIC_DUMP_END]
```

#### AGENT_LOG 매크로
모든 에이전트 관련 커널 로그를 일관된 형식으로:
```
[AGENT][info][pid=1][parser] received task: log_line_42
```

CC가 `grep "[AGENT]"`로 한 번에 추출 가능.

### 4.8 `bench/summarize.py`, `bench/report.py`

25개 JSON 파일을 통째로 CC에 첨부하면 컨텍스트 폭발. 요약본만 CC에 전달:
```json
{"speedup": {"mean": 2.34, "stdev": 0.12, "n": 5}, "isolation_rate": 1.0, ...}
```

---

## 5. Claude Max 20x 운용 전략

### 5.1 자원의 실제 크기

| 항목 | 추정치 |
|---|---|
| 5시간 윈도우 메시지 (짧은 대화) | ~900 |
| 5시간 윈도우 메시지 (자율 작동, 긴 컨텍스트) | ~150~250 |
| 주간 Sonnet 시간 | 240~480시간 |
| 하루 자율 작동 capacity | 8~10시간 |
| 14주 누적 capacity | 약 140시간 |

### 5.2 Opus vs Sonnet 분배

| 작업 유형 | 비율 | 모델 |
|---|---|---|
| 핵심 설계 (proc 구조, 스케줄러 알고리즘) | ~15% | Opus |
| 디버깅 막힘 (30분 이상) | ~5% | Opus (1회 한정) |
| 보고서·슬라이드 영어 polish | ~10% | Opus |
| 시스템 콜 추가, 반복 패턴 | ~30% | Sonnet |
| 테스트 코드, autotest 해석 | ~20% | Sonnet |
| pipe/proxy 코드 | ~15% | Sonnet |
| 커밋 메시지, 상태 갱신 | ~5% | Sonnet |

### 5.3 컨텍스트 절약

1. **큰 로그 파일 통째로 읽기 금지** → `head/tail/wc -l`
2. **벤치 결과 25개 첨부 금지** → `summarize.py` 출력만
3. **샘플 입력 통째로 읽기 금지** → 코드에서 처리
4. **`grep -rn`을 `find + cat`보다 선호**

이 4가지만 지켜도 토큰 소모가 30~50% 감소한다.

---

## 6. 4인 팀 git worktree 배치

### 6.1 배치 원칙

병렬 작업 스트림을 4개로 분할하되, **의존성 충돌이 적은 영역**으로 나눈다.

```
liberal_os/                      # main (사람만 push)
liberal_os-kernel/                # feature/kernel — xv6 커널 수정
liberal_os-agent/                 # feature/agent — Proxy + 에이전트 유저 프로그램
liberal_os-bench/                 # feature/bench — 평가/벤치
liberal_os-harness/                # feature/harness — 하네스 인프라
```

### 6.2 worktree 셋업

```bash
cd ~/liberal_os
git worktree add ../liberal_os-kernel feature/kernel
git worktree add ../liberal_os-agent  feature/agent
git worktree add ../liberal_os-bench  feature/bench
git worktree add ../liberal_os-harness feature/harness
```

각 worktree에서 별도 터미널로 `claude` 실행.

### 6.3 충돌 회피 규약

- **`xv6-src/kernel/`** 만지는 작업은 항상 `kernel` worktree
- **`xv6-src/user/agent_*.c`** 는 항상 `agent` worktree
- **`bench/`, `host/proxy_daemon.py`** 는 명확히 분리
- 공유 파일(`CLAUDE.md`, `MASTER_PLAN.md` (Part II))은 메인에서 심볼릭 링크

### 6.4 머지 흐름

```
agent → main           (사람 검토 후)
kernel → main          (사람 검토 후, regression 통과 필수)
bench → main           (자유롭게)
harness → main         (자유롭게)
```

머지 후 다른 worktree에는 다음 작업 시작 시 `git pull --rebase`로 흡수.

### 6.5 QEMU 포트 격리

각 worktree의 `Makefile`에 다른 GDB 포트 설정:
```makefile
# kernel worktree
QEMU_GDB_PORT = 26000
# agent worktree
QEMU_GDB_PORT = 26100
# bench worktree
QEMU_GDB_PORT = 26200
# harness worktree
QEMU_GDB_PORT = 26300
```

4개 QEMU가 동시 실행되어도 충돌 없음.

---

## 7. 일일 운영 매뉴얼

### 7.1 아침 (사람 30분)

1. 어제 마지막 자율 작동의 `BLOCKED.md` 읽기
2. 각 worktree의 `git log --oneline -10`로 진행 확인
3. 머지 가능한 브랜치를 main에 머지
4. HUMAN GATE 작업 (예: T-50, T-51) 도달 여부 확인 후 승인/거부
5. 필요 시 `MASTER_PLAN.md` (Part II)에 새 작업 추가
6. 각 worktree에 `claude` 실행 + "다음 작업 진행하세요" 한 줄 입력

### 7.2 점심 체크 (사람 5분)

1. `BLOCKED.md` 추가된 항목 있는지 확인
2. 있으면 즉시 결정 내려주기

### 7.3 저녁 (사람 20분)

1. 오늘 완료된 작업 검토
2. 머지 가능한 것 머지
3. 다음 날 작업 우선순위 조정 (필요 시 `MASTER_PLAN.md` (Part II) 수정)

### 7.4 주간 (팀 회의 1시간)

- `MASTER_PLAN.md` (Part II) 전체 검토
- Phase 진행률 확인
- HUMAN GATE 작업 배치 결정
- 막힌 항목 해결 방향 합의
- 차주 worktree 배치 조정

### 7.5 자율 작동의 안정 상태 (Week 11 이후)

```
사람 시간: 50분/일 (아침 30 + 점심 5 + 저녁 15)
CC 자율: 8~10시간/일 × 4 worktree = 32~40 작업시간/일
```

이 비율(사람 1 : CC 자율 40)이 하네스 엔지니어링의 ROI다.

---

## 8. 실패 시나리오와 대응

### 8.1 CC가 같은 검증을 반복 실패

**증상**: 30분 동안 같은 에러 반복

**원인**:
- 검증 명령이 잘못 정의됨
- 의존성 부족 (이전 작업이 완료되지 않음)
- xv6 빌드 환경 문제

**대응**: CC는 `BLOCKED.md`에 자동 기록 후 다음 작업으로. 사람이 아침 체크 시 결정.

### 8.2 CC가 의도와 다른 파일 수정

**증상**: `MASTER_PLAN.md` (Part II)의 `files:` 외 파일이 변경됨

**원인**: 작업 정의가 모호하거나, CC가 컨텍스트 누락으로 잘못 판단

**대응**:
- `CLAUDE.md` §2.2의 규약 위반 → 즉시 `git reset HEAD <file>`로 되돌리기
- 작업 정의를 더 엄격하게 갱신

### 8.3 xv6 부팅 회귀

**증상**: `make autotest`가 부팅 단계에서 실패

**원인**: 커널 수정으로 부팅 자체가 깨짐

**대응**:
1. `regression.sh`가 커밋을 차단했어야 정상
2. 그래도 발생했다면: `git revert HEAD` 또는 `git checkout daily-YYYYMMDD` 백업 태그로 복귀
3. `BLOCKED.md`에 root cause 기록

### 8.4 LLM 한도 초과

**증상**: `claude` 명령이 한도 메시지 반환

**대응**:
- Extra usage 활성화 (Console Billing 설정)
- 한도 리셋까지 사람이 직접 작업 (보고서·슬라이드 등 LLM 호출 적은 작업)
- 모델을 Sonnet으로 강제 (Opus 한도가 별도)

### 8.5 4개 worktree 동시 머지 충돌

**증상**: 같은 파일(예: `Makefile`)을 여러 worktree가 동시 수정

**대응**:
- `MASTER_PLAN.md` (Part II)에서 같은 파일을 만지는 작업은 의존성으로 직렬화
- 발생 시 사람이 수동 머지

---

## 9. 한계와 정직한 평가

### 9.1 하네스가 못 하는 것

- **창의적 설계 판단** — "재시도 카운터를 3회로 할지 5회로 할지" 같은 결정은 사람이 한다
- **xv6 GDB 대화형 디버깅** — 자동 스크립트로 어느 정도 보완하지만 한계 있음
- **발표 슬라이드 디자인 미감** — 영어 polish는 가능, 디자인은 사람
- **팀원 간 의사소통** — 회의·이슈 조율은 사람만 가능

### 9.2 14주 일정에서의 현실적 기대

- **Week 10 (하네스 부트스트랩)**: CC 자율 비율 50%. 하네스가 미완성이라 사람 개입 많음.
- **Week 11~12 (본격 구현)**: CC 자율 비율 80~90%. 하네스 효과 최대.
- **Week 13 (실험·보고서)**: CC 자율 비율 60~70%. 분석·해석은 사람 비중 증가.
- **Week 14 (발표)**: CC 자율 비율 30%. 발표는 사람 작업.

### 9.3 "수십 시간 자율 작동"의 의미

- ❌ 사람 개입 0으로 프로젝트 완성
- ✅ 누적 100~140시간 CC 작업 중 사람 개입 시간 10~20시간 (전체의 7~14%)
- ✅ 단일 세션 자율 작동 4~6시간 (5시간 윈도우 풀 활용)
- ✅ 병렬 worktree 4개로 동일 시간에 4배 진행

이게 현실적이고 검증 가능한 목표다.

---

## 부록 A — 하네스 부트스트랩 체크리스트 (Week 10)

순서대로:

- [ ] T-01 `.claude/settings.json` 작성 (사람, 10분)
- [ ] T-02 `.env.example`, `.gitignore` 정비 (사람, 10분)
- [ ] T-03 xv6 빌드 환경 검증 (사람, 30분)
- [ ] `CLAUDE.md` 프로젝트 루트에 복사 (사람, 1분)
- [ ] `MASTER_PLAN.md` (Part II) 프로젝트 루트에 복사 (사람, 1분)
- [ ] `HARNESS.md` (이 파일) 프로젝트 루트에 복사 (사람, 1분)
- [ ] `BLOCKED.md` 빈 파일 생성 (사람, 1분)
- [ ] 4개 git worktree 셋업 (사람, 10분)
- [ ] 각 worktree에서 `claude` 첫 실행 → "T-04부터 진행" 지시 (사람, 5분)
- [ ] 첫 자율 작동 4시간 후 결과 검토 (사람, 30분)

**총 사람 시간 100분**. 이 100분의 ROI가 이후 14주를 결정한다.

---

## 부록 B — 자주 묻는 질문

**Q. 하네스 없이 그냥 사람이 직접 시키면 안 되나?**
A. 가능하지만 비효율적이다. 같은 14주에 단일 작업 스트림만 진행 가능. 하네스 + worktree로 4배 진행.

**Q. CC가 잘못된 코드를 작성하면?**
A. `regression.sh`가 커밋을 차단한다. 차단 통과한 회귀는 daily 백업 태그로 복귀.

**Q. HUMAN GATE 작업이 사람 일정과 안 맞으면?**
A. 비동기 운영. CC는 HUMAN GATE 도달 시 `BLOCKED.md` 기록 후 의존성 없는 다음 작업으로 이동. 사람이 아침에 승인하면 그때 진행.

**Q. 이 하네스를 다른 프로젝트에 재사용 가능한가?**
A. `CLAUDE.md`와 `MASTER_PLAN.md` (Part II)는 프로젝트 특화. 하지만 5가지 기둥 구조와 worktree 운영 방식은 일반화 가능.

**Q. 보고서에 하네스 엔지니어링 자체를 언급해도 되나?**
A. 권장. Development Process Document (가이드 §5 산출물 #3)에서 "AI 코딩 도구의 책임감 있는 활용"이 평가 기준이라면, 하네스 설계는 그 자체로 엔지니어링 산출물이다. 단, 핵심 구현(xv6 커널 수정)을 CC가 했다는 사실은 정직하게 명시한다.

---

## 마지막으로

하네스 엔지니어링은 **"코드를 잘 작성하는 것"이 아니라 "코드 작성 환경을 잘 설계하는 것"** 이다. Liberal_OS 프로젝트의 14주는 이 환경의 품질에 직접 좌우된다.

좋은 하네스는 보이지 않는다 — 일이 그냥 진행될 뿐이다. 나쁜 하네스는 매일 매시간 마찰을 만든다.

Week 10 첫 주를 하네스에 투자하는 것을 두려워하지 말라.
