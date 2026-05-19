# BLOCKED.md — Claude Code 막힘 보고

> CC가 막혔을 때 자동 기록하는 파일. 사람이 매일 아침 확인.
> 형식은 CLAUDE.md §10 참조.

---

## T-03: xv6 빌드 환경 검증 — BLOCKED at 2026-05-19

**증상**: 시스템에 RISC-V 크로스 컴파일러 / QEMU / make가 설치되어 있지 않음.
`/usr/bin/`에 `make`, `gcc`, `qemu-system-riscv64`, `riscv64-linux-gnu-gcc` 모두 부재.

**시도한 것**:
- `command -v make gcc qemu-system-riscv64 riscv64-linux-gnu-gcc` → 모두 not found
- `dpkg -l | grep -iE 'qemu|riscv'` → 결과 없음

**가설**: 새로 셋업된 WSL2 환경에 빌드 의존성이 미설치.

**필요한 사람 결정**: sudo로 의존성 설치 필요. CLAUDE.md §5에서 `sudo *`는 차단됨.

권장 설치 명령 (Debian/Ubuntu 계열):

```bash
sudo apt update
sudo apt install -y build-essential gdb-multiarch qemu-system-misc \
    gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu \
    python3 python3-pip
```

설치 후 검증:

```bash
cd /home/kusc/Liberal_OS/xv6-src && make qemu
# xv6 셸 프롬프트 `$ ` 보이면 OK. `Ctrl-A X`로 종료.
```

이 작업이 끝나면 본 BLOCKED 항목을 삭제하고 T-05/T-06/T-09 등 빌드 의존 작업을 진행할 수 있다.
