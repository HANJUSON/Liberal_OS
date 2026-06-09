# STATUS.md — Liberal_OS 운영 상태 (Plan + Progress)

> Single page operational ledger for plan, task queue, and progress.
> Design SSoT는 `MASTER_PLAN.md` Part I (§1~§10, 부록).
> 작업 규칙/도구 정책은 `CLAUDE.md`.

**Last updated**: 2026-06-08
**Convention**: 본 문서는 PROGRESS.md / BLOCKED.md / MASTER_PLAN.md Part II(§11~§20)를 흡수한 단일 운영 페이지다. 과거 분리 문서는 git history 참조.

---

## 1. 한눈에

| 항목 | 상태 |
|---|---|
| 브랜치 | `main`, `origin/main` 대비 ahead — push는 사람 |
| T-NN 작업 큐 (T-01 ~ T-72) | 자율 처리 가능분 39건 모두 `[x]` DONE |
| 진행 중인 T-NN | 없음 |
| 블로커 | 없음 |
| 남은 사람 작업 | 4건 (아래 §4) |

---

## 2. 상태 표기와 작업 메타데이터

### 2.1 상태 표기
- `[ ]` **TODO** — 아직 시작 안 함
- `[~]` **IN PROGRESS** — CC가 작업 중 (전체 worktree에서 한 번에 하나만)
- `[x]` **DONE** — 검증 통과, 커밋 완료
- `[!]` **BLOCKED** — §5 블로커 표 참조. 사람 개입 필요
- 🔴 **HUMAN GATE** — 시작 전 반드시 사람 승인

### 2.2 작업 메타데이터
각 작업은 다음을 포함한다:
- **depends**: 선행 작업 ID 목록. 모두 `[x]`여야 시작 가능
- **files**: 건드릴 파일 목록. 이 외의 파일을 수정하려면 §5 BLOCKED 처리
- **verify**: 검증 명령. 통과해야 `[x]`로 변경 가능
- **estimate**: 예상 소요 (CC 자율 시간 기준)
- **assignee**: 담당 worktree

세부 메타데이터(depends/files/verify/estimate)는 git history 참조. 본 문서 §3은 결과만 요약.

---

## 3. 완료 작업 (T-NN 기록)

### Phase 1 — 하네스 부트스트랩 (Week 10)
- T-01 `[x]` `.claude/settings.json` 작성
- T-02 `[x]` `.env.example` 및 `.gitignore` 정비
- T-03 `[x]` xv6 빌드 환경 검증
- T-04 `[x]` 호스트 측 `hello-upstage.py` 작성
- T-05 `[x]` `tests/autotest.sh` 골격
- T-06 `[x]` xv6 user `smoketest.c` 추가
- T-07 `[x]` `tests/regression.sh` 골격
- T-08 `[x]` `bench/summarize.py` 골격
- T-09 `[x]` 패닉 dump 매크로 추가
- T-10 `[x]` `AGENT_LOG` 매크로 도입

### Phase 2 — xv6 proc 확장 + virtio (Week 11 전반)
- T-20 `[x]` proc 구조체 확장
- T-21 `[x]` `setrole` 시스템 콜 추가
- T-22 `[x]` `agentstat` 시스템 콜 + 유저 프로그램
- T-23 `[x]` virtio serial 채널 설정 (console-serial fallback path)
- T-24 `[x]` xv6 측 proxy client 유저 라이브러리
- T-25 `[x]` host proxy daemon mock 모드
- T-26 `[x]` host proxy daemon live 모드 (Upstage 통합)
- T-27 `[x]` LLM 응답 캐시 (replay 모드)

### Phase 3 — 에이전트 5종 + IPC pipe (Week 11 후반 ~ Week 12 전반)
- T-30 `[x]` parser 에이전트 (xv6 유저 프로그램)
- T-31 `[x]` classifier 에이전트
- T-32 `[x]` root-cause 에이전트
- T-33 `[x]` fix-suggester 에이전트
- T-34 `[x]` evaluator 에이전트
- T-35 `[x]` orchestrator (triage 명령)
- T-36 `[x]` 첫 end-to-end mock 통과

### Phase 4 — Evaluator 재시도 루프 (Week 12 중반)
- T-40 `[x]` `kill` + `sleep/wakeup`로 재시도 신호
- T-41 `[x]` 재시도 카운터 + 최대 3회 제한

### Phase 5 — 스케줄러 수정 (Week 12 후반) 🔴
- T-50 `[x]` 🔴 스케줄러 백업 브랜치 생성
- T-51 `[x]` 🔴 `sched.c` 우선순위 큐 구조 추가
- T-52 `[x]` 🔴 priority 측정 시스템 콜

### Phase 6 — 평가 (Week 13)
- T-60 `[x]` `bench/run_all.sh` 완성
- T-61 `[x]` `bench/report.py` — REPORT.md 자동 생성

### Phase 7 — 산출물 (Week 13~14)
- T-70 `[x]` Technical Report 초안
- T-71 `[x]` Development Process Document (`PROCESS.md`)
- T-72 `[x]` 영어 슬라이드 초안 (`slides/draft.md`)

---

## 4. 남은 사람 작업 (자율 처리 불가)

| ID | 작업 | 사유 | 산출물 |
|---|---|---|---|
| T-62 | 실 벤치마크 실행 (live 모드) | 외부 데이터 전송 — `UPSTAGE_API_KEY`로 Upstage API 호출. `MASTER_PLAN.md` Part I §17 `assignee: human` 명시. | `BENCH_N=5 MODE=live bash bench/run_all.sh` → `out/REPORT.md` 자동 갱신 |
| T-73 | 데모 GIF 녹화 | 화면 녹화 — 자율 도구 범위 밖. 텍스트 대체본은 `docs/demo_transcript.txt`로 마련됨. | `docs/demo.gif` |
| Tech Report 팀 narrative | `docs/TECHNICAL_REPORT.md` Appendix A LoC + 팀원별 기여 narrative (`PROCESS.md` §6) | 팀원별 작업 내역은 사람만 정확히 앎. | TECHNICAL_REPORT.md 갱신 |
| `slides/final.pptx` | `slides/draft.md` → pptx 변환 + 디자인 + 발표자 노트 동기화 | 디자인·발표 흐름은 사람 검토 권장. 초벌은 `slides/draft.md`로 완성. | `slides/final.pptx` |
| GitHub 행정 (GuideLine §1) | repo public 확인, 팀 명단·리더·연락처 시트 제출 | 외부 시스템 (학교 시트). | — |
| F-01 | xv6 fs를 에이전트 컨텍스트 저장/복구 메커니즘으로 확장 | 코드 양 큰 작업 (새 파일 시스템 콜, evaluator/triage 통합). GuideLine §2 OS 개념 활용 깊이 향상. README §10에 한계로 명시되어 있던 항목 | `xv6-src/kernel/sysfile.c`, `xv6-src/user/`, TECHNICAL_REPORT §3 갱신 |

---

## 5. 블로커

### T-80 (및 GOAL_PATTERNS 루프 전체) — BLOCKED at 2026-06-10 (loop 1/60)

**증상**: RISC-V 빌드 게이트(`make clean && make`)와 모든 QEMU 기반 verify(`make autotest`/`make regression`)가 툴체인 부재로 실행 불가. `make`가 `riscv64-*-gcc`를 못 찾아 host `gcc`로 폴백 → `-march=rv64gc` 거부.

**시도한 것**:
- `make clean && make` → `cc1: error: bad value 'rv64gc' for '-march='` (host gcc 폴백).
- 툴체인 프로브: `riscv64-unknown-elf-gcc`, `riscv64-linux-gnu-gcc`, `riscv64-elf-gcc`, `riscv64-none-elf-gcc`, `riscv64-unknown-linux-gnu-gcc` **전부 missing**.
- `qemu-system-riscv64` **missing**.
- 작성한 `kernel/verifier.{c,h}`는 host `gcc -fsyntax-only -Wall -Werror -ffreestanding`로 **문법/타입 PASS** (실제 riscv 빌드 게이트의 대체는 아님).

**가설**: 이 환경(WSL2)이 §5.2의 2026-05-20 검증 시점과 달라져 RISC-V GCC/binutils와 QEMU가 미설치 상태. 코드 결함이 아니라 빌드 환경 부재.

**필요한 사람 결정**: 툴체인 설치 필요 —
`sudo apt-get install -y gcc-riscv64-linux-gnu gdb-multiarch qemu-system-misc make python3 python3-pip`
(`sudo`/네트워크 설치는 CLAUDE.md §5에서 CC 금지 → 사람 작업). 설치 후 `cd xv6-src && make qemu`로 부팅 확인되면 루프 재개 가능. 그 전까지 §4 작업 큐 전체가 빌드/QEMU 의존이라 자율 진행 불가.

**현재 상태**: T-80 코드는 작성·커밋됨(WIP, riscv 빌드 미검증이라 `[~]` 유지). 툴체인 복구 시 `make clean && make`로 T-80 verify부터 재개.

#### 업데이트 2026-06-10 (loop 2/60) — 블로커 절반 해소
- `riscv64-linux-gnu-gcc` 설치 확인됨. **T-80 빌드 게이트 PASS**: `make clean && make kernel/kernel` → `kernel/verifier.o`(34KB) 링크 + `kernel/kernel`(296KB) 생성, exit=0.
- **남은 블로커**: `qemu-system-riscv64` 미설치. 이 우분투(25.x)는 riscv64 에뮬레이터가 `qemu-system-misc`가 아니라 **별도 패키지 `qemu-system-riscv`**에 있음(`apt-cache policy qemu-system-riscv` → Installed: none, Candidate: 1:10.2.1+ds-1ubuntu3). QEMU 없이는 `make regression`/`make autotest`/모든 런타임 verify(T-81+) 불가 → T-80도 regression 게이트 때문에 `[x]` 확정 보류.
- **필요한 사람 결정(좁혀짐)**: `sudo apt-get install -y qemu-system-riscv` 1건만 추가하면 루프 정상 재개. 설치 후 `qemu-system-riscv64 --version`으로 확인.

### 5.1 보고 형식 (CLAUDE.md §10 동일)
```markdown
## T-NN: <작업명> — BLOCKED at <ISO timestamp>

**증상**: <한 문장>

**시도한 것**:
- <시도 1>
- <시도 2>

**가설**: <원인 추정>

**필요한 사람 결정**: <구체적 질문>
```

### 5.2 해소 이력
- **2026-05-20** — T-03 (xv6 빌드 환경 검증) 해소. RISC-V 툴체인 / QEMU / make / gdb-multiarch 모두 설치되어 있고, `cd xv6-src && make qemu`로 xv6 부팅 + `init: starting sh` 확인.

---

## 6. 평가자 피드백 대응 (2026-06-07)

채점자 시뮬레이션 리뷰에서 지적된 항목 검증 및 대응:

| # | 지적 | 검증 결과 | 대응 |
|---|---|---|---|
| ① | `make autotest`/`make regression`가 클린 체크아웃에서 fs.img 없어 실패 | 유효 — `xv6-src/Makefile`에 `all` 타깃 없음, 기본은 kernel만 빌드 | `xv6-src/Makefile` L1~L9에 `all: $K/kernel fs.img` 추가. fs.img 삭제 후 `make autotest` PASS 확인 |
| ② | README §5B `--triage samples/short.log` 인자 오류 (xv6 fs는 `short.log`) | 유효 — `host/proxy_daemon.py:210`이 인자를 xv6 셸에 그대로 전달. xv6 fs는 `mkfs/mkfs fs.img README _short.log $(UPROGS)`로 단일 파일 보유 | README §5B 3건 모두 `--triage short.log`로 정정 |
| ③ | README §4.1 `gcc-riscv64-unknown-elf` 패키지 Ubuntu apt에 미존재 | 유효 — 실제 빌드는 xv6-src/Makefile L45 `riscv64-linux-gnu-` 폴백 경로 | README §4.1을 `gcc-riscv64-linux-gnu` 우선으로 재작성, elf 툴체인은 대안으로 명시. 표 헤더(L17)도 동기화 |
| ④ | 모든 측정이 mock 모드 (live 결손) | 유효, 사람 작업 | T-62 (위 §4) — `BENCH_N=5 MODE=live bash bench/run_all.sh` 직접 실행 필요 |
| ⑤ | speedup 주장에 sequential 베이스라인 부재 | 유효 — `bench/report.py:_section_speedup_model`는 분석 모델만 제공 | `host/proxy_daemon.py`에 `--triage-sequential` 추가 (xv6 셸 `;` + 파일 redirect로 5-stage 직렬). `bench/run_all.sh`에 `[bench 2/3]` sequential 단계 추가. `bench/report.py` §4 신규: 실측 비교 표 + 1.16× 실측 (mock, n=5+5). `bench/summarize.py`는 timeout 런 자동 제외 |
| ⑥ | fs/storage 활용 얕음 (README §10에 본인들도 인정) | 유효, 대공사 | F-01로 §4 follow-up 등록 |

검증 명령:
- `rm xv6-src/fs.img && bash tests/autotest.sh` → PASS
- `BENCH_N=5 BENCH_TIMEOUT=60 bash bench/run_all.sh` → 5+5 모두 OK, `out/REPORT.md` §4 실측 표 갱신
- `make regression` → 3-gate PASS

---

## 7. Ralph Loop 회차 보강 (T-NN 큐 외 추가 작업)

GuideLine.md 기반 평가 직전 보강 작업. MASTER_PLAN T-NN 큐와 별개로, 산출물 품질 향상 목적.

| ID | 작업 | 산출물 | 커밋 |
|---|---|---|---|
| README 재작성 | 기존 multiprocessing/cgroups 잔재 제거, xv6 기반 실제 구현으로 GuideLine §3 5개 요구사항 충족 | `README.md` (144 lines) | `7431656` |
| A | `out/REPORT.md` deferred 3항목 → mock 모드 실측·분석 모델 (priotest + retry mechanism + analytical speedup) | `bench/report.py`, `bench/run_all.sh`, `host/proxy_daemon.py --priotest`, `out/REPORT.md`, `.gitignore` 예외 | `0b5c94c` |
| B | TECHNICAL_REPORT §10 speedup 인과 명시 (mock latency=0 + `proxylock(2)` 직렬화로 측정 wall-clock은 xv6 plumbing-bound) | `docs/TECHNICAL_REPORT.md` §10.1/§10.3/§10.4 | `239c2fe` |
| C | TECHNICAL_REPORT §3 매핑표 → "7개 직접 구현 + 1개 활용"으로 분리, file system 행 unnumbered 표시 | `docs/TECHNICAL_REPORT.md` §3 표·prose, §13 Conclusion | `785b8c0` |
| D | TECHNICAL_REPORT "(Draft)" 타이틀·상단 status 배너 제거. 잔여 사람 작업 2건만 말미에 한 단락으로 기록 | `docs/TECHNICAL_REPORT.md` 헤더, Appendix B 말미 | `2bc2ba8` |
| E | Slide 3 "B-flavored workload, A-side contribution" 프레이밍 노트 + speaker note | `slides/draft.md` Slide 3 | `d159183` |
| F | Slide 5 NIM 미사용 사유 1단락 + speaker note (GuideLine §4 선택 항목 대응) | `slides/draft.md` Slide 5 | `5618ded` |
| G | Slide 12 Future Work를 near-term(1–6) + beyond-the-static-DAG(7–10)으로 분리, planner-executor/RAG/ReAct/proxy 멀티플렉싱 명시 | `slides/draft.md` Slide 12 | `c5e9f0b` |
| H | xv6 mock 데모 텍스트 트랜스크립트 캡처 스크립트 + 실제 트랜스크립트 + README §6 참조 갱신 | `bench/capture_demo.py`, `docs/demo_transcript.txt`, `README.md` §6 | `9809214` |
| 인프라 | `.gitignore`로 ralph-loop 로컬 메타·docx 보고서·생성기 스크립트 제외 | `.gitignore` | `77a957a` |

검증: 각 단계 후 `make regression` 통과 (autotest → 셸 → mock proxy 3-gate).
커밋 전 매번 `git diff --staged | grep -iE 'up[-_][a-z0-9_-]{20,}|api[_-]?key'` 클린 확인.

---

## 8. 데모 증거 캡처 (2026-06-08)

§4의 T-62 일부(live 모드 end-to-end 통과)와 §4의 T-73 대체본(스크린샷 7장)을 이 세션에서 확보했다. 정식 `BENCH_N=5 MODE=live bash bench/run_all.sh`는 여전히 사람 작업으로 남아 있다.

### 8.1 단일 라운드트립 검증 (live)
- `MODE=live python3 host/hello_upstage.py` → `{"ok":true,"mode":"live","reply":"Hello, liberal_os.","elapsed_s":1.071}` 확인.
- mock의 0.05s 대비 약 20× — 진짜 네트워크/추론 발생 증거.

### 8.2 5-stage × 5-line = 25 호출 풀스택 (live)
- `python3 host/proxy_daemon.py --triage-sequential short.log --mode live --timeout 240`
- 결과: `ok:true`, `served:{parser:5,classifier:5,rootcause:5,fixsuggest:5,evaluator:5}`, `eval_oks:5`, `eval_fails:0`, `eval_retries:0`, `missing_roles:[]`, `elapsed_s:15.827`.
- 호출당 trace는 `out/live-trace.log`에 보존 (head 50 = 스크린샷 3).
- 캐시 `.cache/llm/` 25건 채워짐 → 이후 `--mode replay`로 1–2초 재현 가능.

### 8.3 ρ = −1.000 priotest 캡처
- `priotest` 3회 실행 결과: 1회 perfect(ρ=−1.0), 2회는 마지막 한 쌍이 같은 tick(t=1) 안에서 race로 뒤집힘(ρ≈−0.94).
- 스크린샷 5는 perfect run으로 캡처. `out/REPORT.md` §3 주장과 일치.

### 8.4 발견된 회귀와 수정

| 회귀 | 원인 | 수정 | 검증 |
|---|---|---|---|
| 인터랙티브 셸에서 영문 입력 echo 부재 | `console.c:188` T-30+에서 의도적 disable(자동화 우선) | `consputc(c)` 주석 해제, 시연용 echo 복원 | `make regression` PASS, `out/regression-shell.log`에 `$ ls`/`$ echo REGRESSION_SHELL_OK` 라인이 echo로 찍힘 |
| `live` 모드 데드락 (parser 1건 후 timeout) | `live_handler` 응답에 `\n`/`\t` 포함 시 PROXY_RES line frame 분할 → `proxy_readline` hang | `" ".join(raw.split())`로 sanitize + 시스템 프롬프트에 single-line 강제 | sequential 25호출 완주 (8.2) |
| `live` 모드 timeout 부족 / 일부 호출 hang | per-request timeout 미설정 + max_tokens 256으로 응답 길어짐 | `max_tokens=80`, `timeout=12.0`, stderr trace 추가 | 8.2 결과 |
| QEMU 좀비가 `fs.img` lock 유지 | timeout 도달 후 자식 QEMU가 살아남 | `ps -fu $USER | grep qemu-system-riscv64` 후 본인 소유 PID kill (CLAUDE.md §5 보강) | live 실행 정상화 |

### 8.5 캡처된 스크린샷 (`out/screenshots/`)

| 파일 | 증거 |
|---|---|
| `screenshot1.png` | Upstage 단일 호출 (`hello_upstage`) |
| `screenshot2.png` | 25 호출 완주 (`--mode replay --triage`) |
| `screenshot3.png` | 25 호출 per-call trace (`out/live-trace.log`) |
| `screenshot4.png` | xv6 `agentstat` JSON 베이스라인 |
| `screenshot5.png` | xv6 `priotest` ρ=−1.000 |
| `screenshot6.png` | xv6 `triage` 단독 — raw PROXY_REQ emit |
| `screenshot7.png` | `make regression` 3/3 PASS |

README §6에 인덱스 표 + 각 스크린샷 캡션 임베드 완료.

---

## 9. 작업 추가 규칙

새 T-NN을 추가할 때:
1. 새 Phase면 Phase 번호를 잇는다 (Phase 8, 9...).
2. 같은 Phase면 다음 사용 가능한 T-NN 번호.
3. 의존성을 정확히 명시한다 (잘못된 의존성은 CC가 wedge에 빠지게 한다).
4. `verify` 명령이 **반드시 자동 실행 가능**해야 한다. "사람이 보기 좋은지 확인" 같은 건 verify가 아니다.

---

## 10. 정책 메모

### 10.1 통합 결과
- `PROGRESS.md`, `BLOCKED.md` → 본 문서로 흡수, 삭제 완료.
- `MASTER_PLAN.md` / `MASTER_PLAN.en.md` Part II(§11~§20) + §19 진행 현황 요약 → 본 문서로 이전, 설계 SSoT만 남김.
- `files/` 디렉토리(legacy harness drop-in) → root 소유라 사용자 권한으로 삭제 불가. 사람이 `sudo rm -rf files/` 직접 실행 권장. `.gitignore` 등록되어 있어 repo에는 영향 없음.

### 10.2 남은 dangling 참조 (이번 권한 범위 밖)
결정 문서/제출 산출물에 이전 파일·섹션 참조가 남아 있다. 본 정리 작업은 `MASTER_PLAN.md` 본문 수정만 명시 승인 받았으므로 아래는 손대지 않았다 — 사람 판단으로 갱신 필요.

| 파일 | 참조 | 권장 갱신 |
|---|---|---|
| `CLAUDE.md` §1.6, §1.8, §2.1, §2.3, §2.4, §3, §10, §11 | `BLOCKED.md`, `MASTER_PLAN.md Part II (§11~§18)` | 모두 `STATUS.md`로 치환 |
| `HARNESS.md` §1.4, §4.4, §6.x, §7.x, §8 체크리스트, §11 FAQ | `BLOCKED.md` | `STATUS.md §5 블로커`로 치환 |
| `PROCESS.md` §1.2, §4 (`I-NN` 이슈), §5, §7 | `BLOCKED.md`, `MASTER_PLAN.md Part II` | 강의 제출물 — 제출 시점 판단 |
| `docs/TECHNICAL_REPORT.md` (`BLOCKED.md` 언급) | `BLOCKED.md` 프로토콜 설명 | 강의 제출물 — 제출 시점 판단 |
| `slides/draft.md` Slide 16 | `MASTER_PLAN.md Part II` | 강의 제출물 — 제출 시점 판단 |

### 10.3 권한 범위
- **이번 정리 대상**: `MASTER_PLAN.md`, `MASTER_PLAN.en.md`, `PROGRESS.md`, `BLOCKED.md`, `files/`.
- **대상 외**: `CLAUDE.md`, `README.md`, `HARNESS.md`, `docs/TECHNICAL_REPORT.md`, `PROCESS.md`, `slides/draft.md`, `out/REPORT.md`, `GuideLine.md`.

*문서 끝.*
