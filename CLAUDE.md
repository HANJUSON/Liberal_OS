# CLAUDE.md — Liberal_OS xv6 프로젝트 작업 지침

> 이 파일은 Claude Code가 모든 세션 시작 시 자동으로 읽는 운영 매뉴얼이다.
> 작업을 시작하기 전 반드시 끝까지 정독한다.

---

## 0. 프로젝트 정체성

**Liberal_OS** — xv6 커널을 직접 수정하여 LLM 에이전트 5개(parser / classifier / root-cause / fix-suggester / evaluator)를 OS 레벨 메커니즘으로 오케스트레이션하는 시스템.

- **방향**: GuideLine §2의 A (OS for LLM)
- **반드시 기억할 한 줄**: "API를 multiprocessing으로 호출"이 아니라 **"LLM 프로세스를 xv6 커널이 직접 관리"**.
- **설계 통합본**: `MASTER_PLAN.md` Part I 설계 참조 (본문 수정 금지) · **작업 큐**: `STATUS.md §3` 참조. T-NN 상태(`[ ]`/`[~]`/`[x]`)는 CC가 작업 진행에 따라 갱신.
- **하네스 설계 근거**: `HARNESS.md` 참조

---

## 1. 절대 규칙 (위반 시 작업 중단)

1. **xv6 커널을 직접 수정한다.** Linux 커널 기능(cgroups, namespaces 등)이나 Python multiprocessing은 사용 금지. 가이드라인 "얇은 래퍼" 경고 대상.
2. **anthropic SDK 코드 작성 금지.** LLM 백엔드는 Upstage Solar Pro 3 (OpenAI 호환) 패턴만:
   - `base_url='https://api.upstage.ai/v1'`
   - `model='solar-pro'`
3. **xv6는 네트워크 스택이 없다.** LLM 호출은 반드시 Linux 호스트의 Proxy Daemon 경유 (virtio serial / pipe).
4. **API 키는 `.env`에서만 로드.** 코드/주석/테스트/커밋 메시지에 절대 하드코딩 금지. 커밋 전 `git diff | grep -iE 'up[-_][a-z0-9_-]{20,}'`로 자가 검증.
5. **평가 측정 시 반드시 `--mode replay` 사용.** LLM 비결정성 통제.
6. **`scheduler()` 함수, `proc.c`의 핵심 부분, `sched.c` 전체는 🔴 HUMAN GATE.** 수정 전 반드시 사람 승인 대기. `STATUS.md §5`에 사유 기록 후 다음 작업으로.
7. **`git push` 금지.** push는 사람이 직접 수행. `.claude/settings.json`에서 차단되어 있어야 정상.
8. **`MASTER_PLAN.md` 본문(Part I, 부록), `README.md`, `HARNESS.md` 수정 금지.** 이 세 파일은 결정 문서. 변경 사항이 있다면 사람에게 보고. `STATUS.md §3`의 T-NN 상태 갱신만 예외적으로 허용.

---

## 2. 작업 흐름 (Work Loop)

매 작업은 다음 순서를 정확히 따른다.

### 2.1 작업 시작
1. `git status` — 작업 트리가 깨끗한지 확인
2. `git pull --rebase` — 다른 worktree의 최신 변경 흡수
3. `STATUS.md §3` 열기 — `[ ]` TODO 중 의존성(`depends:`)이 모두 `[x]`인 가장 작은 번호 작업 선택
4. 해당 작업을 `[~]` IN PROGRESS로 상태 변경, 즉시 커밋 (`chore(tasks): start T-NN`)

### 2.2 작업 수행
1. 작업에 명시된 **건드릴 파일**(`files:`)만 수정. 다른 파일을 만지고 싶으면 `STATUS.md §5`에 사유 기록 후 사람에게 보고.
2. 30분(약 20개 메시지)마다 자가 진단:
   - 같은 검증이 반복 실패 중인가? → 중단, `STATUS.md §5` 기록
   - 의도와 다른 파일을 만지고 있나? → 중단, 원점 복귀
   - 추측으로 디버깅하고 있나? → 데이터(로그, stack trace)부터 본다

### 2.3 검증
1. 작업에 명시된 **검증 명령**(`verify:`) 실행
2. 통과 시: `tests/regression.sh` 추가 실행 (회귀 게이트)
3. 두 가지 모두 통과해야 다음 단계로
4. 실패 시: `out/last-fail.log`, `out/last-fail.diff` 읽기 — 추측 금지

### 2.4 작업 종료
1. `STATUS.md §3`의 해당 작업 상태를 `[x]` DONE으로 변경
2. 커밋: `feat(T-NN): <한 줄 요약>` (Conventional Commits)
3. 커밋 전 `git diff --staged | grep -iE 'up[-_][a-z0-9_-]{20,}|api[_-]?key'` 자가 검증 (API 키 누설 방지)
4. 절대 `git push` 하지 않는다

---

## 3. 디렉토리 규약

```
liberal_os/
├── CLAUDE.md          # 이 파일 (읽기만, 수정 금지)
├── MASTER_PLAN.md     # 설계 마스터 (Part I, 수정 금지; T-NN 상태는 STATUS.md §3에서 갱신)
├── MASTER_PLAN.en.md  # MASTER_PLAN.md의 영문판 (수정 금지)
├── HARNESS.md         # 하네스 설계 문서 (수정 금지)
├── STATUS.md          # CC가 막혔을 때 보고하는 파일 (§5 blocker ledger), T-NN 작업 기록(§3)
├── README.md          # 기술 카탈로그 (수정 금지)
├── .env.example       # 키 자리표시 (실 키는 .env, gitignore)
├── .claude/
│   └── settings.json  # 도구 권한 설정
├── xv6-src/           # xv6 소스 (수정 대상)
│   ├── kernel/        # 커널 코드 (proc.h, proc.c, sched.c, syscall.c 등)
│   └── user/          # 유저 프로그램 (triage, agentstat 등)
├── host/              # Linux 호스트 측 코드
│   └── proxy_daemon.py
├── tests/             # 자동 테스트
│   ├── autotest.sh    # 60초 헤드리스 xv6 부팅 + smoke
│   ├── regression.sh  # 커밋 전 회귀 게이트
│   ├── inputs/        # 자동 입력 파일
│   └── gdb_on_panic.sh
├── bench/             # 평가 스크립트
│   └── run_all.sh
├── samples/           # 입력 로그 샘플 (수정 금지)
├── .cache/llm/        # LLM 응답 캐시 (gitignore)
└── out/               # 결과·로그 (gitignore)
    ├── last-fail.log
    ├── last-fail.diff
    └── bench/
```

---

## 4. 코드 스타일

### C (xv6 커널/유저)
- xv6의 기존 스타일을 따른다 (K&R 스타일, 4-space 들여쓰기).
- 새 함수는 짧은 영어 주석 1줄 + 인자 설명.
- 커널 로그는 `AGENT_LOG(level, fmt, ...)` 매크로 사용 (없으면 추가).
- `panic()` 대신 가능한 한 에러 리턴. 진짜 unrecoverable한 경우만 panic.

### Python (host/, bench/)
- Python 3.11+, type hints 필수
- 외부 의존성은 `host/requirements.txt`에 명시 (`openai`, `python-dotenv` 등)
- 함수 docstring 1줄 요약 + Args/Returns
- 출력은 가능하면 **JSON 한 줄** (CC가 파싱하기 쉽게)

### 공통
- 주석은 **영어**로 작성 (보고서·슬라이드 영어 작업 단축)
- 변수명/함수명도 영어
- 커밋 메시지는 Conventional Commits (`feat`, `fix`, `chore`, `test`, `docs`)

---

## 5. 도구 사용 규약

### 자주 쓰는 명령
| 목적 | 명령 |
|---|---|
| xv6 자동 부팅 + 스모크 테스트 | `make autotest` |
| 커밋 전 회귀 게이트 | `make regression` |
| GDB 첨부 모드로 xv6 부팅 | `make qemu-gdb` |
| Mock 모드 단일 시연 | `make demo MODE=mock` |
| 벤치마크 (사람만 실행) | `bash bench/run_all.sh` |

### 금지 명령
- `sudo *` — `.claude/settings.json`에서 차단되어야 정상
- `git push` — 사람만 수행
- `rm -rf *`, `git reset --hard` — 사전 사람 승인 없이 금지
- `make clean` 후 즉시 `git commit` — 빌드 산출물이 커밋되지 않게 주의

### 빌드 캐시 함정
- xv6 헤더만 수정하고 `.c`는 안 건드린 경우 `make`가 stale 캐시 사용.
- **헤더(.h) 수정 후에는 반드시 `make clean && make`.**

### QEMU 좀비 방지
- `autotest.sh` 첫 줄에 `pkill -f qemu-system-riscv64 2>/dev/null || true` 포함.
- 타임아웃으로 죽인 QEMU가 다음 실행을 방해하는 사고 방지.

---

## 6. LLM 백엔드 — Upstage 패턴

### 호스트 측 (`host/proxy_daemon.py`)
```python
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
        {'role': 'user', 'content': user_prompt},
    ],
)
text = response.choices[0].message.content
```

### xv6 측
xv6 안에서는 LLM의 존재를 모른다. 단지 Proxy Daemon으로 read/write:
```c
// pseudocode
int fd = open("/proxy", O_RDWR);
write(fd, request_json, len);
read(fd, response_json, BUFSIZE);
```

### 모드 (`MODE` 환경변수 또는 `--mode` 플래그)
- `live` — 실 Upstage API 호출 (시연 시에만)
- `replay` — 캐시된 응답만 사용 (평가 시 필수, 비결정성 통제)
- `mock` — `time.sleep(uniform(0.3, 1.0))` + 더미 응답 (autotest/CI용, 기본값)

---

## 7. 디버깅 규약

### xv6 패닉 발생 시
1. **추측 금지.** `out/last-fail.log`를 먼저 읽는다.
2. `[PANIC_DUMP_BEGIN] ... [PANIC_DUMP_END]` 구간을 찾는다.
3. 거기에 죽은 시점의 proc 상태가 dump되어 있다.
4. 그래도 부족하면 `make qemu-gdb` + `tests/gdb_on_panic.sh` 실행 → 결과 텍스트만 읽기.

### virtio/pipe 데드락 의심 시
- Proxy Daemon의 stderr를 확인 (`out/proxy.log`)
- 5초 timeout 초과 시 명시적 로그가 찍히도록 되어 있다.
- 양쪽 모두 read 대기면 둘 중 하나가 write 누락한 것.

### LLM 응답 이상
- `mode replay` 시 캐시 파일 (`.cache/llm/<sha256>.pkl`) 직접 확인 가능.
- 캐시가 없는데 replay 모드면 명시적 에러 발생 — 정상 동작.

---

## 8. 컨텍스트 절약

### 큰 파일 읽기 금지
- `samples/*.log` 파일은 1,000~5,000줄. **절대 전체를 읽지 마라.**
- 필요 시 `head -50`, `tail -50`, `wc -l`로 부분만 확인.
- 전체 처리는 코드에서.

### bench 결과 첨부 금지
- `out/bench/*.json` 25개를 다 읽으면 컨텍스트 폭발.
- `bench/summarize.py`를 만들어 평균·표준편차만 출력. 그것만 읽는다.

### autotest 출력 처리
- 통과 시: 마지막 1~3줄만 본다 (`PASS` 확인).
- 실패 시: `out/last-fail.log`의 마지막 50줄만 본다.

### 코드 검색
- `find`, `grep -rn`을 적극 사용. 파일 전체 읽기보다 빠르고 토큰 절약.
- 함수 정의 위치는 `grep -rn "^[a-zA-Z_].*<func_name>("`.

---

## 9. 모델 선택 가이드

Max 20x 요금제 기준. 작업 유형에 따라 모델 결정:

### Opus를 쓸 때 (~15% 작업)
- xv6 `proc` 구조체 확장 같은 핵심 설계
- 스케줄러 알고리즘 결정
- 디버깅 30분 이상 막혔을 때 (1회 한정, 그래도 안 풀리면 `STATUS.md §5`)
- Technical Report 구조 설계, 영어 논리 검토

### Sonnet을 쓸 때 (기본, ~85% 작업)
- 시스템 콜 추가 (반복 패턴)
- pipe 코드, IPC wrapper 작성
- 테스트 코드 작성
- autotest 결과 파싱, 커밋 메시지 생성
- Proxy Daemon 일반 로직

---

## 10. 막혔을 때

다음 중 하나면 즉시 작업 중단, `STATUS.md §5`에 기록:

- 동일 검증을 3번 이상 실패
- 30분 이상 같은 파일을 고치고 있음
- xv6가 부팅 자체를 못함 (회귀)
- Upstage API rate limit (mock 모드로 전환)
- 의도와 다른 파일을 수정해야 할 것 같은 상황
- HUMAN GATE 작업에 도달

**`STATUS.md §5` 형식**:
```markdown
## T-NN: <작업명> — BLOCKED at <ISO timestamp>

**증상**: <한 문장>

**시도한 것**:
- <시도 1>
- <시도 2>

**가설**: <원인 추정>

**필요한 사람 결정**: <구체적 질문>
```

기록 후 다음 의존성 없는 작업으로 이동. 의존성 있는 작업만 남았으면 작업 종료.

---

## 11. 자기 검증 체크리스트 (커밋 전)

매 커밋 전 다음을 자가 확인:

- [ ] `make autotest` 통과
- [ ] `make regression` 통과
- [ ] `git diff --staged | grep -iE 'up[-_][a-z0-9_-]{20,}|api[_-]?key'` 결과 없음
- [ ] 수정 파일이 `STATUS.md §3` 해당 작업의 `files:` 목록과 일치
- [ ] `STATUS.md §3`의 T-NN 상태가 적절히 갱신됨
- [ ] 커밋 메시지가 Conventional Commits 형식

하나라도 빠지면 커밋하지 않는다.

---

## 12. 끝으로

이 매뉴얼은 살아있는 문서다. 작업 중 명확하지 않은 부분이 있으면 `STATUS.md §5`에 기록하고 사람에게 보고한다. **추측보다 보고를, 추정보다 데이터를.**
