# LLM 멀티에이전트 시스템 — OS 레벨 구현 가이드

> 팀 프로젝트: 장점 · 구현 방법 · 평가 및 모니터링

---

## 목차

1. [멀티에이전트의 핵심 장점](#1-멀티에이전트의-핵심-장점)
2. [OS 레벨 구현 방법](#2-os-레벨-구현-방법)
3. [장점 확인 및 평가 방법](#3-장점-확인-및-평가-방법)
4. [종합 모니터링](#4-종합-모니터링)

---

## 1. 멀티에이전트의 핵심 장점

### 1-1. 진정한 병렬 처리

멀티프로세스는 Python GIL을 완전히 우회하여 CPU 코어 수만큼 실제 병렬 실행이 가능합니다. LLM API 호출처럼 I/O 바운드 작업은 멀티스레드만으로도 충분한 병렬화가 가능합니다.

- **멀티프로세스**: GIL 우회, CPU 집약 작업에 효과적
- **멀티스레드**: 가볍고 메모리 공유 가능, API 호출에 최적
- **속도 향상 목표**: 2x 이상 (에이전트 수에 비례)

### 1-2. 프로세스 격리 & 장애 격리

한 에이전트 프로세스가 크래시해도 다른 에이전트에 영향을 주지 않습니다. 메모리 누수가 해당 프로세스에만 국한되며, 에이전트별 독립적인 환경변수 및 설정이 가능합니다.

- 자식 프로세스 크래시 → 부모 프로세스 생존
- 에이전트별 독립 메모리 공간
- OS 레벨의 강제 종료 및 재시작 가능

### 1-3. 메모리 사용량 제어

OS 레벨에서 프로세스별 메모리 한도를 설정할 수 있습니다. `ulimit`, `cgroups`를 통해 에이전트별 리소스를 정밀하게 제어하고 프로파일링할 수 있습니다.

- **ulimit**: 프로세스별 소프트/하드 메모리 한도
- **cgroups**: 에이전트 그룹 단위 리소스 격리
- **psutil**: 실시간 메모리 사용량 모니터링

### 1-4. 스케줄링 & 우선순위 제어

중요한 에이전트에 높은 CPU 우선순위를 부여하고, OS 스케줄러가 자동으로 부하를 분산합니다. `nice` 값(-20 ~ 19)으로 정밀한 우선순위 조정이 가능합니다.

- **오케스트레이터**: `nice -10` (높은 우선순위)
- **백그라운드 에이전트**: `nice 15` (낮은 우선순위)
- `renice` 명령으로 실행 중 동적 변경 가능

---

## 2. OS 레벨 구현 방법

### 2-1. 아키텍처 패턴 비교

| 구현 방법 | 난이도 | 병렬성 | 메모리 공유 | 적합한 상황 |
|---|---|---|---|---|
| 멀티스레드 | 낮음 | I/O 병렬 | 공유 가능 | LLM API 호출 병렬화 |
| 멀티프로세스 | 중간 | 완전 병렬 | 불가 (IPC) | CPU 집약적 전처리 포함 |
| Unix 파이프 | 낮음 | 순차 | 불가 | 에이전트를 독립 프로그램으로 |
| 소켓 기반 | 높음 | 완전 병렬 | 불가 | 다른 서버에 분산 배포 |
| 공유 파일시스템 | 낮음 | 비동기 | 파일로 공유 | 단순 프로토타입 |

### 2-2. 멀티프로세스 기반

각 에이전트를 독립 프로세스로 실행하는 방식으로, GIL을 완전히 우회하여 진정한 병렬 처리가 가능합니다. `Queue`를 통해 에이전트 간 메시지를 전달합니다.

```python
from multiprocessing import Process, Queue
import anthropic

def agent_process(name, system_prompt, input_queue, output_queue):
    client = anthropic.Anthropic()
    while True:
        task = input_queue.get()
        if task == 'STOP': break
        response = client.messages.create(
            model='claude-sonnet-4-20250514',
            max_tokens=1000,
            system=system_prompt,
            messages=[{'role': 'user', 'content': task}]
        )
        output_queue.put({'agent': name, 'result': response.content[0].text})
```

### 2-3. 멀티스레드 기반

I/O 바운드 작업(LLM API 호출)에 가장 효율적인 방식입니다. 프로세스보다 가볍고 메모리를 공유할 수 있으며, `Lock`으로 공유 자원을 보호합니다.

```python
import threading
from queue import Queue

lock = threading.Lock()
shared_context = {}  # 스레드 간 공유 메모리

class AgentThread(threading.Thread):
    def run(self):
        task = self.in_queue.get()
        result = call_llm(self.system_prompt, task)
        with lock:  # 공유 메모리 보호
            shared_context[self.name] = result
        self.out_queue.put(result)
```

### 2-4. Unix 파이프 기반

에이전트를 완전히 독립된 프로그램으로 분리하고 Unix 파이프로 연결합니다. 각 에이전트는 `stdin`으로 입력을 받고 `stdout`으로 다음 에이전트에게 전달합니다.

```bash
# 파이프라인 실행 (셸 명령)
python researcher.py | python analyst.py | python writer.py
```

```python
# researcher.py
import sys, anthropic
task = sys.stdin.read().strip()
response = client.messages.create(...)
print(response.content[0].text)  # stdout → 다음 에이전트
```

### 2-5. 소켓 기반 분산 에이전트

에이전트들이 네트워크 소켓으로 통신하며, 다른 머신에 분산 배포가 가능합니다. 오케스트레이터가 각 에이전트 서버를 호출하여 결과를 수집합니다.

```python
# 에이전트 서버 (agent_server.py)
import socket, threading

def run_agent_server(host, port, system_prompt):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind((host, port))
        s.listen()
        while True:
            conn, _ = s.accept()
            threading.Thread(target=handle_client,
                             args=(conn, system_prompt)).start()

# 오케스트레이터에서 에이전트 호출
research = call_agent('localhost', 9001, 'AI 트렌드 조사')
analysis = call_agent('localhost', 9002, research)
```

---

## 3. 장점 확인 및 평가 방법

### 3-1. 병렬 처리 효과 측정

순차 실행과 병렬 실행의 시간을 비교하여 speedup 비율을 계산합니다. 목표 speedup은 **2x 이상**입니다.

```python
import time

# 순차 실행 시간
start = time.perf_counter()
for task in tasks:
    agent_func(task)
sequential_time = time.perf_counter() - start

# 병렬 실행 시간
start = time.perf_counter()
threads = [Thread(target=agent_func, args=(t,)) for t in tasks]
[t.start() for t in threads]
[t.join() for t in threads]
parallel_time = time.perf_counter() - start

speedup = sequential_time / parallel_time
print(f'속도 향상: {speedup:.2f}x')
```

OS 명령어로도 실시간 확인이 가능합니다.

```bash
# 실시간 CPU 코어 사용률 확인
htop -d 5

# 프로세스별 CPU/메모리 사용률
ps aux | grep python

# 특정 PID의 스레드 목록
ps -T -p <PID>
```

### 3-2. 장애 격리 확인

자식 프로세스를 의도적으로 크래시시킨 후 부모 프로세스가 정상 동작하는지 확인합니다.

```python
from multiprocessing import Process, Queue
import os

def fault_isolation_test(in_q, out_q):
    try:
        task = in_q.get()
        if 'crash' in task:
            raise RuntimeError('에이전트 크래시!')
        out_q.put(f'성공: {task}')
    except Exception as e:
        out_q.put(f'실패: {e}')  # 프로세스만 종료

p = Process(target=fault_isolation_test, args=(in_q, out_q))
p.start()
# 자식 크래시 후에도 메인 프로세스 생존 확인
print(f'메인 프로세스 PID: {os.getpid()}')  # 정상 출력됨
```

```bash
# 프로세스 트리 확인
pstree -p <메인_PID>

# 자식 강제 종료 후 부모 생존 확인
kill -9 <자식_PID>
ps aux | grep python
```

### 3-3. 메모리 사용량 확인

```python
import resource, psutil, os

def monitor_memory(pid):
    proc = psutil.Process(pid)
    info = proc.memory_info()
    print(f'RSS: {info.rss / 1024 / 1024:.1f} MB')
    print(f'VMS: {info.vms / 1024 / 1024:.1f} MB')
```

```bash
# cgroups로 메모리 한도 설정 (Linux)
cgcreate -g memory:/agent_group
echo 536870912 > /sys/.../memory.limit_in_bytes
cgexec -g memory:/agent_group python agent.py

# 실시간 메모리 모니터링
watch -n 1 'cat /sys/fs/cgroup/memory/agent_group/memory.usage_in_bytes'
```

### 3-4. 우선순위 제어 확인

```python
import os

# 우선순위 설정 (소프트웨어 레벨)
os.setpriority(os.PRIO_PROCESS, orchestrator_pid, -10)  # 높음
os.setpriority(os.PRIO_PROCESS, background_pid, 10)     # 낮음
```

```bash
# 셸에서 우선순위 확인
ps -o pid,ni,pri,cmd -p <PID>

# 실행 중 동적 변경
renice -n 5 -p <PID>
```

---

## 4. 종합 모니터링

### 4-1. AgentMonitor 클래스

모든 에이전트의 CPU, 메모리, 스레드 수, 상태를 통합적으로 수집하고 리포트를 생성합니다.

```python
import psutil, time
from collections import defaultdict

class AgentMonitor:
    def __init__(self, agent_pids: dict):
        self.agent_pids = agent_pids  # {'리서처': pid, ...}
        self.metrics = defaultdict(list)

    def collect(self):
        for name, pid in self.agent_pids.items():
            try:
                proc = psutil.Process(pid)
                self.metrics[name].append({
                    'cpu':     proc.cpu_percent(interval=0.1),
                    'mem_mb':  proc.memory_info().rss / 1024 / 1024,
                    'threads': proc.num_threads(),
                    'status':  proc.status(),
                })
            except psutil.NoSuchProcess:
                print(f'[경고] {name} 프로세스 종료됨')

    def report(self):
        for name, data in self.metrics.items():
            avg_cpu = sum(d['cpu'] for d in data) / len(data)
            max_mem = max(d['mem_mb'] for d in data)
            print(f'[{name}] CPU: {avg_cpu:.1f}%  MEM: {max_mem:.1f}MB')

    def run(self, interval=2, duration=60):
        end = time.time() + duration
        while time.time() < end:
            self.collect()
            time.sleep(interval)
        self.report()
```

### 4-2. 핵심 지표 요약

| 평가 항목 | 측정 방법 | 목표 기준 |
|---|---|---|
| 병렬 처리 효과 | `speedup = 순차시간 / 병렬시간` | 2x 이상 |
| 장애 격리 | 자식 크래시 후 부모 생존 여부 | 100% 생존 |
| 메모리 효율 | `psutil` / `/proc/<PID>/status` | 한도 내 유지 |
| CPU 우선순위 | `ps -o ni` 또는 `htop` NI 컬럼 | 설정값 반영 |
| 전체 자원 사용 | `htop`, `psutil`, `cgroups` 통계 | 코어 활용률 70%+ |
| 에이전트 상태 | `AgentMonitor.collect()` | running 상태 유지 |

### 4-3. 구현 방법 선택 가이드

| 상황 | 권장 방법 |
|---|---|
| LLM API 호출 병렬화 | **멀티스레드** (I/O 바운드에 최적) |
| CPU 집약적 전처리 포함 | **멀티프로세스** (GIL 우회) |
| 에이전트를 독립 프로그램으로 | **Unix 파이프** |
| 다른 서버에 분산 배포 | **소켓 / gRPC** |
| 단순 프로토타입 | **공유 파일시스템** |

---

> **핵심**: 각 에이전트를 블랙박스 프로세스로 취급하고, OS가 제공하는 격리/스케줄링/모니터링 도구를 그대로 활용하는 것이 OS 레벨 멀티에이전트의 핵심입니다.
