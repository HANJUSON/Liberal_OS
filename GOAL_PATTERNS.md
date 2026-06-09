# GOAL_PATTERNS.md — 자율 루프 목표 스펙 (두 패턴 동시 구축)

> 이 파일은 **자율 루프(Ralph Loop 등)의 단일 목표 스펙**이다. 루프는 매 회차 이 파일을 열고
> §4 작업 큐에서 **의존성이 모두 `[x]`인 가장 작은 번호의 `[ ]` 작업**을 골라 수행한다.
> **반드시 먼저 `CLAUDE.md` 전체를 정독**하고 그 작업 흐름(§2)·도구 정책(§5)·디버깅 규약(§7)·
> 막힘 처리(§10)를 그대로 따른다. 이 스펙은 CLAUDE.md를 대체하지 않고 **목표만 추가**한다.
> §6 완료 게이트가 모두 통과하면 루프는 **DONE을 선언하고 종료**한다.
>
> **작업 기준 경로: `/home/kusc/project/os`** (모든 상대 경로는 이 디렉터리 기준).

---

## 0. 목표 (한 문단)

Liberal_OS에 두 핵심 패턴을 **둘 다** 직접 커널 메커니즘으로 구현한다.

- **패턴 A — 검증+롤백 닫힌 루프.** fix-suggester/evaluator가 내놓은 "수정 제안"을
  **커널 측 검증기**가 불변식·범위·보호 프로세스 규칙으로 검사한다. 통과하면 적용(accept), 실패하면
  **마지막 정상 상태로 롤백**하고 실패 사유를 retry_context에 누적해 다음 프롬프트에 주입한 뒤
  기존 `kill + sleep/wakeup` 재시도 메커니즘으로 재시도한다(최대 3회). LLM은 *제안자*일 뿐,
  최종 권한은 커널에 있다.
- **패턴 B — 커널 시맨틱 캐시 단락.** 에이전트가 `proxy_call()`로 LLM을 호출하기 전,
  **커널 내 응답 캐시**를 먼저 조회한다. FNV-1a 정확 매치 미스 시 **MinHash 시그니처의 Jaccard
  유사도(정수비 임계)**로 의역 프롬프트까지 hit 처리해 LLM 호출(=PROXY_REQ 송출) 자체를 스킵한다.
  RAM 슬롯 + `/cache.bin` 디스크 오버레이 2계층.

두 패턴 모두 **신규 시스콜·신규 커널 .c 파일**을 만들어 OS 메커니즘의 폭과 LLM 통합의 정교함을
함께 높인다.

---

## 1. 절대 제약 (위반 시 그 작업 중단·기록 후 다음 작업)

1. **무인 실행 = 전부 MOCK 모드.** 사람이 자는 동안 돌므로 **네트워크·API 키·live 호출 금지.**
   모든 verify는 `MODE=mock`에서 결정론적으로 통과해야 한다. live/replay는 건드리지 않는다.
2. **🔴 HUMAN GATE 절대 엄수.** `scheduler()` 함수, `proc.c`의 스케줄러 핵심부, `sched.c` 전체는
   수정 금지. 두 패턴은 이를 건드리지 않도록 설계됐다. 검증기가 proc 메타데이터가 필요하면
   **`sysproc.c` 시스콜 계층에서 읽어 verifier.c로 값을 넘긴다**(scheduler 미수정).
   만약 어떤 작업이 게이트 코드 수정을 요구하는 것 같으면 **즉시 중단**, `STATUS.md §5`에 블로커
   기록 후 다음 의존성 없는 작업으로.
3. **`git push` 금지.** 사람만 수행.
4. **동결 문서 수정 금지**: `README.md`, `MASTER_PLAN.md`, `MASTER_PLAN.en.md`, `HARNESS.md`,
   `CLAUDE.md`. 변경이 필요하면 `STATUS.md`나 `docs/`에 기록하고 사람에게 보고. 단
   `docs/TECHNICAL_REPORT.md`와 `STATUS.md`, 본 `GOAL_PATTERNS.md`는 갱신 허용.
5. **API 키 누설 금지.** 커밋 전 매번 `git diff --staged | grep -iE 'up[-_][a-z0-9_-]{20,}|api[_-]?key'`
   결과가 비어 있어야 한다.
6. **헤더(.h) 수정 후에는 반드시 `make clean && make`** (CLAUDE.md §5 빌드 캐시 함정).
7. **회귀 게이트.** 각 작업 verify 통과 후 `make regression`이 통과해야 `[x]`. 실패 시 커밋 금지.
8. **건드릴 파일은 각 작업 `files:`로 한정.** 그 외 파일 수정이 필요하면 블로커 기록.

---

## 2. 신규 식별자 예약 (충돌 방지)

- 시스콜 번호: `SYS_verifyfix 27`, `SYS_checkpoint 28`, `SYS_restore 29`,
  `SYS_cacheget 30`, `SYS_cacheset 31`. (현재 마지막은 `SYS_setprio 26`.)
- 신규 커널 파일: `kernel/verifier.c`, `kernel/verifier.h`, `kernel/cache.c`, `kernel/cache.h`.
- 신규 유저 테스트 프로그램: `user/verifiertest.c`, `user/cachetest.c`.
- 신규 테스트 스크립트: `tests/test_verifier.sh`, `tests/test_cache.sh`.
- 디스크 오버레이 파일: xv6 fs 내 `/cache.bin` (mkfs에 포함하거나 첫 set 시 `create`).

---

## 3. 설계 가이드 (구현 시 따를 것 — 추측 금지)

### 패턴 A — 검증기 (verifier.c)
- **순수 함수 중심.** `verify_fix(const struct fix_proposal *p, const struct sys_snapshot *s, char *reason, int rlen) -> int`
  (0 = PASS, 음수 = 위반 코드). 부동소수점 금지(정수만).
- 검사할 불변식(최소):
  1. 필수 필드 존재 + 길이 범위(role, target, action, severity 등) — 구조적 무결성.
  2. 수치 필드 범위 클램프 검사(예: severity 0..3, retry_hint ≥ 0). 범위 밖이면 위반.
  3. action이 허용 목록(예: `{REPORT, ANNOTATE, REQUEUE}`)에 속하는가 — 화이트리스트.
  4. **보호 프로세스 규칙**: 제안이 `init`/`sh`/`evaluator` 등 보호 pid/role을 대상으로
     파괴적 action을 요구하면 위반. 보호 목록은 verifier.h 상수.
- 검증기는 proc 테이블을 **직접 안 만진다.** 필요한 proc 상태는 `sys_verifyfix`(sysproc.c)가
  읽어 `sys_snapshot`으로 채워 넘긴다.

### 패턴 A — 체크포인트/롤백
- "마지막 accept된 상태"를 커널 버퍼(고정 크기) 또는 fs 파일에 보관. `sys_checkpoint`가 저장,
  `sys_restore`가 복원. verify 실패 시 restore → "previous state restored" 로그.

### 패턴 A — 재시도 피드백 루프 (evaluator.c / triage.c 통합)
- `evaluator.c`의 기존 `MAX_RETRIES` 루프를 확장: 각 시도에서 fix 제안을 `verifyfix`로 검사 →
  FAIL이면 `restore` + 위반 사유를 retry_context에 append → 기존 `kill+sleep/wakeup` 재시도 신호.
  PASS면 accept하고 `checkpoint`. 최대 3회(기존 T-41 유지). 매 시도 `EVAL_RETRY` 태그 라인 emit.
- 호스트(`host/proxy_daemon.py`)는 retry_context(누적 위반 사유)를 다음 프롬프트에 주입(mock도 반영).

### 패턴 B — 캐시 (cache.c)
- 키 = 프롬프트의 **FNV-1a 64-bit 해시**(원문 미저장, 0은 빈 슬롯 → 충돌 시 1로 승격).
- RAM 슬롯 테이블 + `/cache.bin` 순차 스캔/append 디스크 오버레이, 디스크 hit 시 RAM promote.
- 시맨틱: 단어 단위 shingle → **MinHash 시그니처(SIG_K개)**, double-hashing(`h1 + k*h2`),
  stopword 제외·ASCII 소문자화·구두점 무시. Jaccard 임계는 **정수비**(예: `2/5 = 0.40`).
  정확 매치 미스 시 시그니처 일치 개수 ≥ 임계면 hit.
- `cache_get(role, prompt)` → hit이면 캐시값, miss이면 빈 값. `cache_set(role, prompt, value)`.

### 패턴 B — 단락 통합 (proxy_client.h / 에이전트)
- `proxy_call()` 진입 시 `cacheget` 먼저 → hit이면 **PROXY_REQ를 송출하지 않고** 캐시값 반환.
  miss이면 기존 경로(PROXY_REQ→RES) 후 `cacheset`. (role+prompt 기준 결정론적 캐싱만.)

---

## 4. 작업 큐 (T-80 ~ T-93) — depends 순서대로

### Phase 8 — 패턴 A: 검증+롤백 닫힌 루프
- T-80 `[x]` verifier 골격. files: `kernel/verifier.{c,h}`, `kernel/Makefile`(또는 루트 Makefile OBJS). depends: —. verify: `make clean && make` 빌드 성공 + verifier.o 링크.
- T-81 `[x]` `sys_verifyfix(27)` 배선 + sys_snapshot 채움. files: `kernel/syscall.{c,h}`, `kernel/sysproc.c`, `user/usys.pl`, `user/user.h`. depends: T-80. verify: `user/verifiertest.c`가 good→PASS / bad(보호 pid·범위 밖)→FAIL 단언, `make autotest`에 verifiertest 스모크 통과.
- T-82 `[x]` `sys_checkpoint(28)`/`sys_restore(29)` + 커널 상태 버퍼. files: `kernel/syscall.{c,h}`, `kernel/sysproc.c`, `user/usys.pl`, `user/user.h`. depends: T-81. verify: checkpoint 후 변경→restore→원복 확인(verifiertest 확장 또는 신규 단언).
- T-83 `[x]` evaluator/triage 재시도 루프에 verify→FAIL시 restore+retry, PASS시 checkpoint 통합. files: `user/evaluator.c`, `user/triage.c`. depends: T-82. verify: `tests/test_verifier.sh`가 "VERIFY FAIL → ROLLBACK → RETRY → ACCEPT" 시퀀스를 mock에서 재현(EVAL_RETRY 라인 검출).
- T-84 `[x]` 호스트 retry_context 주입(mock 포함). files: `host/proxy_daemon.py`. depends: T-83. verify: mock 재시도 트랜스크립트에 누적 위반 사유가 다음 프롬프트에 포함됨 확인.

### Phase 9 — 패턴 B: 커널 시맨틱 캐시 단락
- T-85 `[x]` cache 골격 + FNV-1a + RAM 슬롯 exact get/set. files: `kernel/cache.{c,h}`, Makefile OBJS. depends: —. verify: `make clean && make` + `user/cachetest.c` exact hit/miss 단언.
- T-86 `[x]` MinHash 시그니처 + Jaccard 정수비 임계 + stopword/소문자/word-shingle. files: `kernel/cache.c`, `kernel/cache.h`. depends: T-85. verify: cachetest가 의역 프롬프트("list files" vs "please list the files") hit, 무관 프롬프트 miss 단언.
- T-87 `[x]` `/cache.bin` 디스크 오버레이(append/scan/promote). files: `kernel/cache.c`, (필요시 `mkfs/mkfs.c`). depends: T-86. verify: cachetest가 set→(재부팅 시뮬 또는 RAM 슬롯 비움)→디스크 scan hit 단언.
- T-88 `[x]` `sys_cacheget(30)`/`sys_cacheset(31)` 배선 + proxy_client 단락 통합. files: `kernel/syscall.{c,h}`, `kernel/sysproc.c`, `user/usys.pl`, `user/user.h`, `user/proxy_client.h`. depends: T-87. verify: `tests/test_cache.sh`가 동일/의역 프롬프트 반복 시 PROXY_REQ 송출 횟수가 distinct 프롬프트 수보다 적음(=캐시 hit으로 LLM 스킵) 확인.

### Phase 10 — 통합·증거·완료 게이트
- T-89 `[ ]` 두 패턴 동시 동작 e2e: `triage short.log`가 (a) 검증 reject→retry와 (b) 캐시 hit을 모두 보이는 트랜스크립트 캡처. files: `bench/capture_demo.py`(또는 신규 `bench/capture_patterns.py`), `docs/patterns_demo.txt`. depends: T-84, T-88. verify: `docs/patterns_demo.txt`에 "VERIFY FAIL"/"ROLLBACK"/"RETRY"/"ACCEPT"와 "CACHE HIT" 라인 모두 존재.
- T-90 `[ ]` `tests/regression.sh`에 test_verifier.sh + test_cache.sh 편입, autotest 스모크에 verifiertest/cachetest 추가. files: `tests/regression.sh`, `tests/autotest.sh`. depends: T-89. verify: `make regression` 3-게이트+신규 2테스트 모두 PASS.
- T-91 `[ ]` `docs/TECHNICAL_REPORT.md` §3 OS-개념 매핑표에 검증기 시스콜·캐시 서브시스템 행 추가, §5/§10에 두 패턴 설계·근거 서술. files: `docs/TECHNICAL_REPORT.md`. depends: T-90. verify: 표에 신규 2개 메커니즘 행 존재(grep).
- T-92 `[ ]` `STATUS.md §3`에 Phase 8/9/10 완료 기록, README 갱신 필요분은 **수정하지 말고** §5에 "사람 갱신 권장" 한 줄로 기록. files: `STATUS.md`. depends: T-91. verify: STATUS에 T-80~T-92 결과 요약 존재.
- T-93 `[ ]` **최종 완료 게이트 실행**(§6). files: —. depends: T-92. verify: §6의 모든 항목 PASS → 본 작업을 `[x]`로 표기하고 루프 DONE 선언.

---

## 5. 작업 절차 (매 회차)

1. `git status` 클린 확인. (dirty면 직전 작업 미완 — 이어서 마무리)
2. 본 §4에서 **의존성이 모두 `[x]`인 최소 번호 `[ ]`** 작업 선택, `[~]`로 표기 후 커밋
   (`chore(tasks): start T-NN`).
3. 해당 `files:`만 수정하며 §3 설계 가이드를 따른다. 헤더 수정 시 `make clean && make`.
4. 해당 `verify:` 실행 → 통과 시 `make regression` 추가 실행.
5. 둘 다 통과 → `[x]`로 표기, `feat(T-NN): <요약>` 커밋. 커밋 전 키 누설 grep 자가검증.
6. 실패 시: `out/last-fail.log` 읽고 데이터 기반 수정. **동일 verify 3회 연속 실패 → 중단**,
   `STATUS.md §5`에 블로커 기록 후 다음 의존성 없는 작업으로.
7. 모든 작업이 `[x]`이거나 남은 작업이 전부 블로킹이면 §6 게이트 실행 후 종료.

---

## 6. 완료 게이트 (이 모든 게 PASS여야 DONE — 루프 정지 조건)

루프는 아래를 **순서대로 실행**해 전부 통과할 때만 "DONE"을 선언하고 멈춘다. 하나라도 실패하면
관련 T-NN을 `[ ]`로 되돌리고 계속 작업한다. (모든 명령은 `/home/kusc/project/os`에서 실행.)

```
[ ] G1  make clean && make            # 헤더 추가 후 풀 빌드 성공
[ ] G2  make regression               # 3-게이트 + test_verifier.sh + test_cache.sh 모두 PASS
[ ] G3  bash tests/test_verifier.sh   # "VERIFY FAIL → ROLLBACK → RETRY → ACCEPT" 시퀀스 재현
[ ] G4  bash tests/test_cache.sh      # 의역 프롬프트가 캐시 hit → PROXY_REQ 송출 감소 확인
[ ] G5  test -f docs/patterns_demo.txt && grep -q "CACHE HIT" docs/patterns_demo.txt \
        && grep -q "ROLLBACK" docs/patterns_demo.txt    # 두 패턴 동시 동작 증거
[ ] G6  grep -c "SYS_verifyfix\|SYS_cacheget" kernel/syscall.h  # 신규 시스콜 ≥ 5종 배선
[ ] G7  git diff --staged | grep -iE 'up[-_][a-z0-9_-]{20,}|api[_-]?key'  # 결과 비어 있어야 함
[ ] G8  동결 문서(README/MASTER_PLAN*/HARNESS/CLAUDE) 변경 0건  (git log --stat 확인)
[ ] G9  §4의 T-80 ~ T-92 전부 [x]
```

**DONE 선언 형식** (게이트 전부 통과 시 마지막 메시지):
```
ALL PATTERNS COMPLETE.
- Pattern A (verify+rollback): syscalls 27-29, kernel/verifier.c, test_verifier.sh PASS
- Pattern B (semantic cache):  syscalls 30-31, kernel/cache.c, test_cache.sh PASS
- Evidence: docs/patterns_demo.txt
- regression: PASS | secrets: clean | frozen docs: untouched
사람 후속: live 모드 1회 실행으로 두 패턴 실측, README OS-매핑표 갱신.
```

---

## 7. 반복 횟수·종료 규칙 (무한 루프 방지)

### 7.1 반복 카운트 추적
- 매 회차 **시작 시** `out/loop_count.txt`(gitignore됨)의 정수를 1 증가시켜 기록한다. 파일이 없으면 1로 생성.
- 회차 시작 로그에 `[loop N/60]`을 출력해 현재 진행을 사람이 알 수 있게 한다.

### 7.2 종료 조건 (아래 중 하나라도 충족되면 루프 종료)
1. **완성** — §6 완료 게이트 G1~G9가 전부 PASS. → §6의 `ALL PATTERNS COMPLETE` 형식으로 선언하고 종료. **(정상 종료)**
2. **전부 블로킹** — 남은 `[ ]` 작업이 모두 의존성 미충족 또는 HUMAN GATE라 더 진행 불가. → `STATUS.md §5`에 사유 정리 후 종료.
3. **안전 상한 도달** — `out/loop_count.txt` ≥ **60**. → 완성 못 했어도 종료하고, `STATUS.md §5`에
   "현재까지 완료한 T-NN / 남은 T-NN / 마지막 실패 원인"을 정리한다. 사람이 깨어나 이어받도록 둔다.
   *상한은 "목표"가 아니라 병적 폭주를 막는 백스톱이다. 정상적으로는 40회 내외에서 조건 1로 끝나야 한다.*

### 7.3 회차 내 막힘 처리 (CLAUDE.md §10)
- 같은 파일을 30분(약 20메시지) 이상 고치거나 동일 verify를 3회 연속 실패하면, 그 작업을 블로커로
  돌리고(`STATUS.md §5`) **다른 의존성 없는 작업으로 전환**한다. 한 작업에 매달려 상한을 소진하지 않는다.
- 시간·토큰 제한은 없으나 **목적 없는 반복은 금지** — 진전이 없으면 위 규칙으로 반드시 전환하거나 종료한다.
