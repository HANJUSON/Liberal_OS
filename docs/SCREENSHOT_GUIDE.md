# 스크린샷 촬영 가이드 (Liberal_OS)

> 발표/보고서용 증거 스크린샷을 **직접 찍기 위한 단계별 가이드**.
> 현재 `out/screenshots/screenshot1–7.png`는 2026-06-08(Pattern A/B 도입 *이전*) 촬영본이다.
> 이 가이드는 기존 7장을 갱신하고 **Pattern A/B 증거(현재 스크린샷 없음)** 를 새로 추가하는 것을 목표로 한다.

---

## 0. 준비 (최초 1회)

```bash
cd <repo>/Liberal_OS

# (1) 빌드 의존성 — 이미 깔려 있으면 생략
sudo apt install -y gcc-riscv64-linux-gnu gdb-multiarch qemu-system-misc make python3 python3-venv

# (2) Python venv
python3 -m venv .venv && . .venv/bin/activate
pip install -r host/requirements.txt

# (3) 클린 빌드 + 좀비 QEMU 정리
pkill -f qemu-system-riscv64 2>/dev/null || true
( cd xv6-src && make clean && make ) | tail -3      # kernel + fs.img 생성

# (4) live 스크린샷(1·2·3)을 찍을 거면 .env 에 실제 키
cp .env.example .env        # 편집해서 UPSTAGE_API_KEY=up-... 채우기
mkdir -p out/screenshots
```

> ⚠️ **한 가지만 기억**: 매 실행 전 `pkill -f qemu-system-riscv64` 로 좀비를 치운다.
> 좀비가 `fs.img`를 잡고 있으면 `qemu: Failed to get "write" lock` 이 뜬다.

---

## 1. 촬영 환경 팁

- **터미널을 넓게**(120+컬럼). 데몬이 찍는 JSON 한 줄이 줄바꿈으로 지저분해지지 않게.
- JSON을 예쁘게 보고 싶으면 끝에 ` | python3 -m json.tool` 을 붙인다.
- **xv6 셸 종료**: `Ctrl-A` 누르고 떼고 `X` (QEMU `-nographic` 종료 시퀀스).
- **폰트 키우고**, 프롬프트에 명령이 보이게 한 화면에 *명령 + 핵심 출력*이 같이 담기도록.
- 일반 터미널에서는 `make qemu`(포그라운드)가 정상 동작한다 — 특별한 트릭 불필요.

---

## 2. 촬영 목록 (10장)

각 항목: **명령 → 기대 출력(핵심) → 포착 포인트 → 저장 파일명**.
그룹 A·B는 기존 7장 갱신, **그룹 C·D·E가 신규 Pattern A/B 증거**.

### A. Host-side — Upstage Solar Pro 3 연결 (live, `.env` 키 필요)

**A-1. 단일 호출 round-trip** → `screenshot1.png`
```bash
MODE=live python3 host/hello_upstage.py
```
- 기대: `{"ok": true, "mode": "live", "reply": "...", "elapsed_s": ~1.0}`
- 포착: `ok:true` + `elapsed_s ≈ 1s` (mock의 0.05s와 대비되는 실 API 지연).

**A-2. 25-call 파이프라인** → `screenshot2.png`
```bash
# 처음엔 live로 캐시를 채우며 trace도 남긴다(A-3에서 사용):
python3 host/proxy_daemon.py --triage-sequential short.log --mode live --timeout 240 \
        2> out/live-trace.log
# 이후 재현은 replay가 1~2초로 빠르고 비용 0 (스크린샷은 둘 중 아무거나):
python3 host/proxy_daemon.py --mode replay --triage short.log
```
- 기대: `{"ok": true, ..., "served": {"parser":5,"classifier":5,"rootcause":5,"fixsuggest":5,"evaluator":5}, "missing_roles": [], "eval_oks": 5, ...}`
- 포착: `ok:true` · `served`에 5개 역할 전부 5씩 · `missing_roles:[]` — 5×5=25 LLM 호출 완주.

**A-3. per-call trace** → `screenshot3.png`
```bash
head -50 out/live-trace.log
```
- 기대: `[live] parser CALL ...` / `[live] parser DONE 0.7s -> ...` 쌍이 25회.
- 포착: 호출마다 실제 `CALL → DONE` — "thin wrapper 아님" 증거.

### B. Guest-side — xv6 OS 메커니즘 (`make qemu` 대화형)

```bash
cd xv6-src && make qemu          # 부팅 후 $ 프롬프트
```
부팅 직후 화면 자체가 `init: starting sh` 까지 → 원하면 부팅 화면도 1장.

**B-4. proc 메타데이터** → `screenshot4.png`
```
$ agentstat
```
- 기대: `[{"pid":1,"name":"init","role":"none","prio":0,"st":"sleep"}, ...]`
- 포착: 신규 시스콜 `agentstat(23)` + `struct proc`에 추가한 `role`/`prio`/`st` 필드.

**B-5. 우선순위 스케줄링** → `screenshot5.png`
```
$ priotest
```
- 기대: 6개 자식이 priority 높은 순으로 종료, 마지막에 상관계수 `rho = -1.000`(완벽한 역순).
- 포착: 수정한 `scheduler()`가 `setprio` priority를 존중함.

**B-6. fork+pipe 진입** → `screenshot6.png`
```
$ triage short.log
```
- 기대: `parser` 자식이 raw `PROXY_REQ\t...` 프레임을 찍고, host daemon이 없으니 응답 대기(hang) — **정상**. (`Ctrl-A X`로 빠져나온다.)
- 포착: fork+pipe 파이프라인 첫 단계 진입 + `proxylock` 경로.

> xv6 셸에서 신규 시스콜도 한 장 더: `$ setrole parser` 후 `$ agentstat` 로 role 반영 확인.

### C. Pattern A — 검증+롤백 (신규) → `screenshot8.png`

**옵션 1 (추천, 깔끔):** 호스트 테스트로 시퀀스 요약
```bash
bash tests/test_verifier.sh
```
- 기대: `ok=True eval_retries=2 eval_oks=5` 다음 줄에 `PASS`
- 포착: `eval_retries=2` — 2개 ERROR 라인이 VERIFY FAIL→ROLLBACK→RETRY→ACCEPT를 거쳐 재시도됨.

**옵션 2 (커널 단위 단언):** xv6 셸에서
```
$ verifiertest
```
- 기대: `ok: good proposal (rc=0)` … 5종 검증 + checkpoint/restore 왕복 → `VERIFIER_TEST_PASS`
- 포착: 커널 검증기가 범위 밖(rc=-2)·보호프로세스(rc=-3)·화이트리스트(rc=-4) 위반을 각각 거부.

### D. Pattern B — 시맨틱 캐시 (신규) → `screenshot9.png`

**옵션 1 (추천):** 캐시가 LLM 호출을 줄이는 증거
```bash
bash tests/test_cache.sh
```
- 기대: `ok=True served_parser=5 served_evaluator=5 eval_retries=2 proxy_reqs_saved=2` → `PASS`
- 포착: `proxy_reqs_saved=2` — evaluator 재시도가 캐시 hit으로 PROXY_REQ(=LLM 호출) 2회 생략.

**옵션 2 (커널 단위, 의역 매칭):** xv6 셸에서
```
$ cachetest
```
- 기대: `ok: semantic paraphrase hit (rc=1)` / `ok: disk-scan hit after RAM clear (rc=1)` … → `CACHE_TEST_PASS`
- 포착: "list files" ≡ "please list the files" 의역 hit + `/cache.bin` 디스크 영속.

### E. 통합 증거 (신규/갱신)

**E-7. 회귀 게이트 (기존 screenshot7 갱신)** → `screenshot7.png`
```bash
make regression
```
- 기대: `==> 1/6 ...` ~ `==> 6/6 ...` 후 `PASS` (※ 기존 스크린샷의 "3/3"을 대체).
- 포착: 6단계(부팅·셸·proxy·**Pattern A**·**Pattern B**·2-패턴 증거) 자동 통과.

**E-10. 두 패턴 한 화면 증거** → `screenshot10.png`
```bash
python3 bench/capture_patterns.py && sed -n '1,13p' docs/patterns_demo.txt
```
- 기대:
  ```
  PATTERNS_DEMO_OK VERIFY_FAIL=2 ROLLBACK=2 RETRY=2 ACCEPT=5 CACHE_HIT=2
  === Liberal_OS two-pattern e2e demo ... ===
  Pattern A — ...:  VERIFY FAIL : 2  ROLLBACK : 2  RETRY : 2  ACCEPT : 5
  Pattern B — ...:  CACHE HIT   : 2
  ```
- 포착: **한 번의 `triage` 실행에서 두 패턴이 동시에** 발생한 카운트.

---

## 3. 권장 일괄 촬영 순서

1. (live 키 있으면) **A-1 → A-2 → A-3** : 캐시/trace를 A-2 live 실행으로 한 번에 만든 뒤 A-3 캡처.
2. **E-7 `make regression`** : 큰 그림(6단계 전부 PASS) 한 장.
3. **C / D / E-10** : `test_verifier.sh` · `test_cache.sh` · `capture_patterns.py` — 패턴 증거 3장.
4. **make qemu** 한 세션에서 **B-4·B-5·B-6 (+ verifiertest·cachetest 옵션2)** 를 연속 캡처 후 `Ctrl-A X`.

> live 키가 없으면 A-1~A-3은 건너뛰고 **A-2를 `--mode replay`** 로 대체(캐시가 이미 채워져 있으면 가능). 나머지(B~E)는 전부 키 없이 mock으로 찍힌다.

---

## 4. 최종 체크리스트

- [ ] 매 실행 전 `pkill -f qemu-system-riscv64`
- [ ] 터미널 120+컬럼, 명령+핵심 출력이 한 화면에
- [ ] **A** Host(live): ok:true / 25-call / per-call trace (1·2·3)
- [ ] **B** Guest: agentstat · priotest(ρ=-1.000) · triage fork+pipe (4·5·6)
- [ ] **C** Pattern A: `eval_retries=2` 또는 `VERIFIER_TEST_PASS` (8)
- [ ] **D** Pattern B: `proxy_reqs_saved=2` 또는 `CACHE_TEST_PASS` (9)
- [ ] **E** 통합: `make regression` 6/6 PASS (7) · two-pattern 카운트 (10)
- [ ] 찍은 파일을 `out/screenshots/`에 저장, README §6 표/캡션과 번호 일치 확인
- [ ] (README는 동결 문서) 새 스크린샷 반영 시 캡션의 "촬영 시점 3-stage" 등 갱신은 사람이 직접
