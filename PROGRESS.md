# PROGRESS.md — Ralph 루프 실행 결과 요약

> 자동 처리 세션 결과. SSoT는 `MASTER_PLAN.md` Part II.
> 본 문서는 Ralph 루프(이번 회차)에서 수행된 작업과 남은 사람 승인 항목을 한 페이지로 정리한다.

**갱신**: 2026-05-27
**처리 정책**: 안전한 태스크만 자율 진행, 승인 필요 태스크는 건너뛰고 본 문서에 기록.

---

## 완료한 태스크 (이번 회차 — GuideLine.md 기반 평가 후속작업)

| ID | 작업 | 산출물 | 커밋 |
|---|---|---|---|
| README 재작성 | 기존 multiprocessing/cgroups 설계 잔재 제거, xv6 기반 실제 구현으로 GuideLine §3 5개 요구사항 충족 | `README.md` (144 lines) | `7431656` |
| A | `out/REPORT.md` deferred 3항목을 mock 모드 실측·분석 모델로 대체 (priotest + retry mechanism + analytical speedup) | `bench/report.py` 전면 개편, `bench/run_all.sh` 재구성, `host/proxy_daemon.py --priotest`, `out/REPORT.md`, `.gitignore` 예외 | `0b5c94c` |
| B | TECHNICAL_REPORT §10에 speedup 인과 명시 — mock latency=0 + `proxylock(2)` 직렬화로 측정 wall-clock은 xv6 plumbing-bound | `docs/TECHNICAL_REPORT.md` §10.1/§10.3/§10.4 | `239c2fe` |
| C | TECHNICAL_REPORT §3 매핑표를 "7개 직접 구현 + 1개 활용"으로 분리, file system 행을 unnumbered로 표시 | `docs/TECHNICAL_REPORT.md` §3 표·prose, §13 Conclusion | `785b8c0` |
| D | TECHNICAL_REPORT "(Draft)" 타이틀·상단 status 배너 제거. 잔여 사람 작업 2건만 말미에 한 단락으로 기록 | `docs/TECHNICAL_REPORT.md` 헤더, Appendix B 말미 | `2bc2ba8` |
| E | Slide 3에 "B-flavored workload, A-side contribution" 프레이밍 노트 + speaker note 추가 | `slides/draft.md` Slide 3 | `d159183` |
| F | Slide 5에 NIM 미사용 사유 1단락 + speaker note 추가 (GuideLine §4 선택 항목 대응) | `slides/draft.md` Slide 5 | `5618ded` |
| G | Slide 12 Future Work를 near-term(1–6) + beyond-the-static-DAG(7–10)으로 분리, planner-executor/RAG/ReAct/proxy 멀티플렉싱 명시 | `slides/draft.md` Slide 12 | `c5e9f0b` |
| H | xv6 mock 데모 텍스트 트랜스크립트 캡처 스크립트 + 실제 트랜스크립트 + README §6 참조 갱신 | `bench/capture_demo.py`, `docs/demo_transcript.txt`, `README.md` §6 | `9809214` |

검증: 각 단계 후 `make regression` 통과 (`autotest` → 셸 → mock proxy 3-gate).
커밋 전 매번 `git diff --staged | grep -iE 'up[-_][a-z0-9_-]{20,}|api[_-]?key'` 클린 확인.

---

## 사람 승인 필요 (건너뜀)

| ID | 작업 | 분류 사유 |
|---|---|---|
| T-62 | 실 벤치마크 실행 (`bash bench/run_all.sh` live 모드) | 외부 데이터 전송 — Upstage live API 호출. `MASTER_PLAN.md` §17 `assignee: human` 명시. |
| T-73 | 데모 GIF 녹화 (`docs/demo.gif`) | 화면 녹화 — 자율 도구 범위 밖. 텍스트 트랜스크립트(`docs/demo_transcript.txt`)로 대체본 마련. |
| Tech Report 팀 narrative | `docs/TECHNICAL_REPORT.md` Appendix A LoC + 팀원별 기여 narrative (`PROCESS.md` §6) | 팀원별 작업 내역은 사람만 정확히 앎. |
| `slides/final.pptx` | 마크다운 → pptx 최종 변환 + 디자인 + 발표자 노트 동기화 | 디자인·발표 흐름은 사람 검토 권장. 초벌은 `slides/draft.md`로 완성. |
| CLAUDE.md §1 규칙 8 갱신 | README.md 동결 규칙 유지 여부 정책 결정 | 정책 결정 — 사람 판단. 이번 회차는 사용자 명시 승인으로 README 재작성을 진행. |
| §1 행정 | GitHub repo public 확인, 팀 명단·리더·연락처 시트 제출 | 외부 시스템 (학교 시트). |

> 위 항목은 안전 분류 기준의 "외부로의 데이터 전송", "되돌릴 수 없는 또는 도구 외 작업",
> "판단이 애매한 모든 것"에 해당하여 본 루프에서는 수행하지 않는다.

---

## 실패한 항목

없음.

- Task A에서 `out/` 디렉토리가 `.gitignore`로 전체 제외되어 있어 `out/REPORT.md`만 예외로 추가하는 `out/*` + `!out/REPORT.md` 패턴으로 우회. 부모 디렉토리 무시 제약(`gitignore(5)`)을 반영한 정상 수정.
- Task A의 retry 효과 측정은 mock 모드 결정성 때문에 quality delta가 구조적으로 0 pp — 이는 mechanism 검증(retry_signals = MAX_RETRIES × final_fails) 형태로 기록하고 quality delta는 T-62 live 모드로 deferred. `out/REPORT.md` §2가 이 인과를 명시.
- `make regression` 모든 단계 통과 (autotest / shell / mock proxy).
- 커밋 전 API 키 누설 검사 모두 클린.

---

## 전체 진행 현황 (MASTER_PLAN.md §19 동기화)

**완료** (`[x]`):
- Phase 1: T-01~T-10 (10건)
- Phase 2: T-20~T-27 (8건)
- Phase 3: T-30~T-36 (7건)
- Phase 4: T-40, T-41 (2건)
- Phase 5: T-50~T-52 (3건) 🔴
- Phase 6: T-60, T-61 (2건)
- Phase 7: T-70, T-71, T-72 (3건)

**이번 회차 추가 보강** (Phase 7 산출물 품질 향상, MASTER_PLAN.md T-NN과 별개로 진행):
- README.md 전면 재작성 — GuideLine §3 충족
- `out/REPORT.md` 3 deferred → 실측·분석 모델로 대체 (Task A)
- TECHNICAL_REPORT §3/§10 정직성·정확성 보강 (Task B, C, D)
- 슬라이드 3·5·12 평가자 질문 선제 차단 + future work narrative (Task E, F, G)
- `docs/demo_transcript.txt` 텍스트 데모 (Task H, T-73 사람 작업 대체본)

**사람 승인 대기**:
- T-62 (실 벤치마크 실행) — human
- T-73 (데모 GIF) — human (텍스트 대체본은 확보)
- Tech Report 팀 narrative + 최종 LoC — human
- `slides/final.pptx` 최종 변환 — human
- GitHub 행정 (§1) — human
- CLAUDE.md README 규칙 변경 정책 결정 — human

**잔여 자율 가능 작업**: 없음.

---

## 다음 행동 가이드

1. **T-62**: API 키가 `.env`에 채워진 머신에서 `BENCH_N=5 bash bench/run_all.sh` (mode를 live로 바꾼 변종, 또는 환경변수 `MODE=live`) 실행. 결과 `out/REPORT.md` 자동 갱신. `MASTER_PLAN.md` Part II T-62를 `[x]`로 변경.
2. **T-73**: `python3 bench/capture_demo.py`를 시연하면서 asciinema로 녹화 → svg-term-cli 또는 OBS GIF 변환하여 `docs/demo.gif` 저장.
3. **Tech Report 마무리**: `PROCESS.md` §6 팀원별 narrative 작성, `cloc xv6-src host bench tests` 결과를 Appendix A에 반영.
4. **`slides/final.pptx`**: `slides/draft.md`를 pandoc 또는 marp로 변환, 디자인 톤 조정.
5. **README 동결 규칙**: 이번 회차에서 사용자 명시 승인으로 변경했으므로, `CLAUDE.md` §1 규칙 8에서 `README.md`를 빼는 것을 고려.

*문서 끝.*
