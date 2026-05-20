# TASKS.md — Liberal_OS 작업 큐

> Claude Code가 자율 작동 시 다음 작업을 결정하는 근거 문서.
> `CLAUDE.md` §2 작업 흐름에 따라 한 번에 하나의 작업만 `[~]` 상태로 둔다.

---

## 상태 표기

- `[ ]` **TODO** — 아직 시작 안 함
- `[~]` **IN PROGRESS** — CC가 작업 중 (전체 worktree에서 한 번에 하나만)
- `[x]` **DONE** — 검증 통과, 커밋 완료
- `[!]` **BLOCKED** — `BLOCKED.md` 참조. 사람 개입 필요
- 🔴 **HUMAN GATE** — 시작 전 반드시 사람 승인

---

## 작업 형식

각 작업은 다음 메타데이터를 포함한다:
- **depends**: 선행 작업 ID 목록. 모두 `[x]`여야 시작 가능
- **files**: 건드릴 파일 목록. 이 외의 파일을 수정하려면 BLOCKED 처리
- **verify**: 검증 명령. 통과해야 `[x]`로 변경 가능
- **estimate**: 예상 소요 (CC 자율 시간 기준)
- **assignee**: 담당 worktree (4인 팀 + 4 worktree 운영 시)

---

## Phase 1 — 하네스 부트스트랩 (Week 10)

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

### T-09 `[~]` 패닉 dump 매크로 추가
- **depends**: T-03, T-06
- **files**: `xv6-src/kernel/printf.c`, `xv6-src/kernel/proc.h`
- **verify**: 일부러 패닉 유발 후 `[PANIC_DUMP_BEGIN]...[PANIC_DUMP_END]` 출력 확인
- **estimate**: 1시간
- **assignee**: kernel

### T-10 `[ ]` `AGENT_LOG` 매크로 도입
- **depends**: T-09
- **files**: `xv6-src/kernel/agent_log.h`, `xv6-src/kernel/defs.h`
- **verify**: 커널에서 `AGENT_LOG("info", "test %d", 42)` 호출 시 `[AGENT][info][...]` 출력
- **estimate**: 30분
- **assignee**: kernel

---

## Phase 2 — xv6 proc 확장 + virtio (Week 11 전반)

> 목표: 에이전트 메타데이터를 xv6 proc에 박고, 호스트와 통신하는 채널 확보.

### T-20 proc 구조체 확장
- **depends**: T-06, T-10
- **files**: `xv6-src/kernel/proc.h`, `xv6-src/kernel/proc.c`
- **verify**: fork 후 자식 proc의 `agent_role`, `priority` 필드가 부모로부터 상속됨 (테스트: `xv6-src/user/test_proc_fields.c`)
- **estimate**: 2시간
- **assignee**: kernel
- **note**: 기본값은 `agent_role = "none"`, `priority = 0`

### T-21 `setrole` 시스템 콜 추가
- **depends**: T-20
- **files**: `xv6-src/kernel/syscall.c`, `xv6-src/kernel/syscall.h`, `xv6-src/kernel/sysproc.c`, `xv6-src/user/user.h`, `xv6-src/user/usys.pl`
- **verify**: 유저 프로그램에서 `setrole("parser")` 호출 후 `agentstat`에 반영됨
- **estimate**: 1시간
- **assignee**: kernel

### T-22 `agentstat` 시스템 콜 + 유저 프로그램
- **depends**: T-21
- **files**: 위와 동일 + `xv6-src/user/agentstat.c`
- **verify**: xv6 셸에서 `agentstat` 실행 시 모든 active proc의 정보를 JSON 1줄로 출력
- **estimate**: 2시간
- **assignee**: kernel

### T-23 virtio serial 채널 설정
- **depends**: T-04
- **files**: `Makefile` (QEMU 옵션), `host/proxy_pipe.py`
- **verify**: 호스트 측 `host/proxy_pipe.py`가 xv6 게스트의 출력을 받고 입력을 보낼 수 있음 (echo 테스트)
- **estimate**: 3시간
- **assignee**: agent
- **note**: 가장 까다로운 작업. virtio가 안 되면 `-chardev pipe`로 fallback

### T-24 xv6 측 proxy client 유저 라이브러리
- **depends**: T-23
- **files**: `xv6-src/user/proxy_client.c`, `xv6-src/user/proxy_client.h`
- **verify**: xv6 유저 프로그램에서 `proxy_call("echo", "hello")` → 호스트 echo 응답 받음
- **estimate**: 2시간
- **assignee**: agent

### T-25 host proxy daemon mock 모드
- **depends**: T-23
- **files**: `host/proxy_daemon.py`
- **verify**: `MODE=mock python host/proxy_daemon.py` 후 xv6에서 LLM 요청 시 더미 응답 받음
- **estimate**: 1시간
- **assignee**: agent

### T-26 host proxy daemon live 모드 (Upstage 통합)
- **depends**: T-25, T-04
- **files**: `host/proxy_daemon.py`
- **verify**: `MODE=live`로 xv6에서 짧은 LLM 요청 → solar-pro 응답
- **estimate**: 1시간
- **assignee**: agent

### T-27 LLM 응답 캐시 (replay 모드)
- **depends**: T-26
- **files**: `host/proxy_daemon.py`, `.cache/llm/`
- **verify**: 동일 입력 두 번 호출 시 두 번째는 캐시 사용 (timestamp 비교로 확인)
- **estimate**: 1시간
- **assignee**: agent

---

## Phase 3 — 에이전트 5종 + IPC pipe (Week 11 후반 ~ Week 12 전반)

> 목표: 5종 에이전트가 xv6 안에서 pipe로 연결되어 동작.

### T-30 parser 에이전트 (xv6 유저 프로그램)
- **depends**: T-22, T-24
- **files**: `xv6-src/user/agent_parser.c`
- **verify**: 표준입력으로 로그 라인 받아 proxy_call 후 구조화된 출력
- **estimate**: 2시간
- **assignee**: agent

### T-31 classifier 에이전트
- **depends**: T-30
- **files**: `xv6-src/user/agent_classifier.c`
- **verify**: parser 출력 → 분류 결과 (INFO/WARN/ERROR/CRITICAL) 출력
- **estimate**: 1.5시간
- **assignee**: agent

### T-32 root-cause 에이전트
- **depends**: T-31
- **files**: `xv6-src/user/agent_rootcause.c`
- **verify**: ERROR/CRITICAL 입력만 처리, 그 외는 패스스루
- **estimate**: 1.5시간
- **assignee**: agent

### T-33 fix-suggester 에이전트
- **depends**: T-32
- **files**: `xv6-src/user/agent_fixsuggest.c`
- **verify**: root-cause 출력 받아 수정 제안 생성
- **estimate**: 1.5시간
- **assignee**: agent

### T-34 evaluator 에이전트
- **depends**: T-33
- **files**: `xv6-src/user/agent_evaluator.c`
- **verify**: 각 단계 출력을 검증 후 통과/재시도 신호 출력
- **estimate**: 3시간
- **assignee**: agent

### T-35 orchestrator (triage 명령)
- **depends**: T-30, T-31, T-32, T-33, T-34
- **files**: `xv6-src/user/triage.c`
- **verify**: `triage samples/short.log` 실행 시 5개 에이전트 fork + pipe 연결 + 결과 출력
- **estimate**: 2시간
- **assignee**: agent

### T-36 첫 end-to-end mock 통과
- **depends**: T-35
- **files**: `tests/e2e_mock.sh`
- **verify**: `MODE=mock make e2e` 통과
- **estimate**: 1시간
- **assignee**: bench

---

## Phase 4 — Evaluator 재시도 루프 (Week 12 중반)

### T-40 `kill` + `sleep/wakeup`로 재시도 신호
- **depends**: T-34, T-35
- **files**: `xv6-src/kernel/proc.c` (필요 시 sleep/wakeup 확장), `xv6-src/user/agent_evaluator.c`
- **verify**: Evaluator가 worker에게 retry 신호 보내면 worker가 재실행
- **estimate**: 4시간
- **assignee**: kernel + agent (협업)

### T-41 재시도 카운터 + 최대 3회 제한
- **depends**: T-40
- **files**: `xv6-src/user/agent_evaluator.c`
- **verify**: 일부러 검증 실패하는 mock 응답으로 정확히 3회 재시도 후 실패 보고
- **estimate**: 1시간
- **assignee**: agent

---

## Phase 5 — 스케줄러 수정 (Week 12 후반) 🔴

> 이 Phase는 모두 HUMAN GATE. xv6 부팅 자체를 깰 수 있음.

### T-50 🔴 스케줄러 백업 브랜치 생성
- **depends**: T-36
- **files**: (사람이 수행) `git checkout -b backup/before-scheduler`
- **verify**: 백업 브랜치 존재 확인
- **estimate**: 5분
- **assignee**: human

### T-51 🔴 sched.c 우선순위 큐 구조 추가
- **depends**: T-50
- **files**: `xv6-src/kernel/proc.c`의 `scheduler()` 함수
- **verify**: `make autotest` 통과 (회귀 없음) + 우선순위 적용 테스트 통과
- **estimate**: 4시간 (이 중 사람 검토 1시간)
- **assignee**: kernel (사람 승인 후)

### T-52 🔴 priority 측정 시스템 콜
- **depends**: T-51
- **files**: `xv6-src/kernel/sysproc.c`, `xv6-src/user/user.h`
- **verify**: 같은 워크로드를 다른 priority로 실행 시 처리 시간 차이 측정
- **estimate**: 2시간
- **assignee**: kernel

---

## Phase 6 — 평가 (Week 13)

### T-60 `bench/run_all.sh` 완성
- **depends**: T-36, T-41, T-52
- **files**: `bench/run_all.sh`
- **verify**: 5개 실험 5회 반복 후 `out/bench/*.json` 생성
- **estimate**: 2시간
- **assignee**: bench

### T-61 `bench/report.py` — REPORT.md 자동 생성
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

## Phase 7 — 산출물 (Week 13~14)

### T-70 Technical Report 초안
- **depends**: T-62
- **files**: `docs/TECHNICAL_REPORT.md`
- **verify**: 5,000~8,000 단어, §3.2 OS 매핑표가 첫 장에 포함
- **estimate**: 4시간 (Opus 권장)
- **assignee**: human + claude

### T-71 Development Process Document
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

## 작업 추가 규칙

새 작업을 추가할 때:
1. 새 Phase면 Phase 번호를 잇는다 (Phase 8, 9...).
2. 같은 Phase면 다음 사용 가능한 T-NN 번호.
3. 의존성을 정확히 명시한다 (잘못된 의존성은 CC가 wedge에 빠지게 한다).
4. `verify` 명령이 **반드시 자동 실행 가능**해야 한다. "사람이 보기 좋은지 확인" 같은 건 verify가 아니다.

---

## 마지막 갱신

2026-05-20 — Phase 1 ~70% 완료 ([x]: T-01/02/03/04/05/06/08).
다음 진행 후보: T-07 → T-09 → T-10.
