#!/usr/bin/env python3
"""Linux-only, verified closed-loop TCP benchmark; Python standard library only.

Owns a fresh server per case. No changes to server limits or implementation.
One outstanding request per working connection; client and server CPUs separated.
"""

import argparse
import collections
import datetime
import hashlib
import json
import math
import multiprocessing as mp
import os
from pathlib import Path
import platform
import selectors
import socket
import struct
import subprocess
import time
import traceback


KEYS = 10_000
VALUE_BYTES = 256
SLOW_KEY = b"__bench_slow"
VALUES = [(f"value{i:05d}_".encode() + b"v" * VALUE_BYTES)[:VALUE_BYTES]
          for i in range(KEYS)]
GETS = [f"GET key{i:05d}\n".encode() for i in range(KEYS)]
SETS = [f"SET key{i:05d} ".encode() + VALUES[i] + b"\n" for i in range(KEYS)]
REPLIES = [value + b"\n" for value in VALUES]


def connect():
    sock = socket.create_connection(("127.0.0.1", 8080), timeout=5)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return sock


def expect(sock, expected):
    received = bytearray()
    while len(received) < len(expected):
        chunk = sock.recv(len(expected) - len(received))
        if not chunk:
            raise RuntimeError("early EOF")
        received.extend(chunk)
    if received != expected:
        raise RuntimeError("incorrect response")


def worker(worker_id, clients, cpu, begin, go, ready, warmup, duration, output):
    sockets = []
    try:
        os.sched_setaffinity(0, {cpu})
        selector = selectors.DefaultSelector()
        states = []
        for index in range(clients):
            sock = connect()
            sockets.append(sock)
            sock.sendall(b"COUNT\n")
            expect(sock, f"{KEYS + 1}\n".encode())
            sock.setblocking(False)
            state = dict(sock=sock, random=1 + worker_id * 1009 + index,
                         sequence=index % 10, received=bytearray())
            states.append(state)
            selector.register(sock, selectors.EVENT_READ, state)
        ready.put((worker_id, None))
        go.wait(20)
        if not go.is_set():
            raise RuntimeError("start barrier timed out")
        run_start = begin.value
        measure_start = run_start + int(warmup * 1e9)
        end = measure_start + int(duration * 1e9)
        time.sleep(max(0, (run_start - time.monotonic_ns()) / 1e9))
        histogram = collections.Counter()
        per_second = [0] * math.ceil(duration)
        counts = collections.Counter()
        cpu_start = None
        cpu_end = None
        maximum_ns = 0
        warmup_completed = 0
        drained = 0

        def transmit(state):
            try:
                sent = state["sock"].send(state["pending"])
            except BlockingIOError:
                sent = 0
            state["pending"] = state["pending"][sent:]
            selector.modify(state["sock"], selectors.EVENT_WRITE
                            if state["pending"] else selectors.EVENT_READ, state)

        def issue(state):
            # Fixed-seed PRNG for repeatable uniform key selection. Every ten
            # requests contain eight GETs and two SETs. SET keeps each key's
            # known value, allowing exact verification despite concurrent writes.
            state["random"] = (1664525 * state["random"] + 1013904223) & 0xffffffff
            key = state["random"] % KEYS
            is_set = state["sequence"] % 10 >= 8
            state["sequence"] += 1
            state["kind"] = "SET" if is_set else "GET"
            state["expected"] = b"OK\n" if is_set else REPLIES[key]
            state["pending"] = memoryview(SETS[key] if is_set else GETS[key])
            state["received"].clear()
            state["started"] = time.monotonic_ns()
            transmit(state)

        for state in states:
            issue(state)
        active = clients
        while active:
            now = time.monotonic_ns()
            if cpu_start is None and now >= measure_start:
                cpu_start = time.process_time()
            if cpu_end is None and now >= end:
                cpu_end = time.process_time()
            if now > end + 5_000_000_000:
                raise RuntimeError("requests failed to drain within five seconds")
            for key, events in selector.select(0.1):
                state = key.data
                if events & selectors.EVENT_WRITE:
                    transmit(state)
                    continue
                try:
                    chunk = state["sock"].recv(4096)
                except BlockingIOError:
                    continue
                if not chunk:
                    raise RuntimeError("server closed working connection")
                state["received"].extend(chunk)
                expected = state["expected"]
                if len(state["received"]) < len(expected):
                    continue
                completed = time.monotonic_ns()
                if state["received"] != expected:
                    raise RuntimeError("incorrect or surplus response bytes")
                if measure_start <= completed < end:
                    latency = completed - state["started"]
                    histogram[(latency + 999) // 1000] += 1
                    maximum_ns = max(maximum_ns, latency)
                    counts[state["kind"]] += 1
                    per_second[min(int((completed - measure_start) / 1e9),
                                   len(per_second) - 1)] += 1
                elif completed < measure_start:
                    warmup_completed += 1
                else:
                    drained += 1
                if completed < end:
                    issue(state)
                else:
                    selector.unregister(state["sock"])
                    state["sock"].close()
                    active -= 1
        if cpu_end is None:
            cpu_end = time.process_time()
        result = dict(worker=worker_id, clients=clients, cpu=cpu,
                      counts=dict(counts), histogram_us=dict(histogram),
                      per_second=per_second, maximum_ns=maximum_ns,
                      warmup_completed=warmup_completed, drain_completed=drained,
                      cpu_seconds=cpu_end - (cpu_start or cpu_end), errors=0)
    except Exception:
        result = dict(worker=worker_id, error=traceback.format_exc())
        ready.put((worker_id, result["error"]))
    finally:
        for sock in sockets:
            sock.close()
    Path(output).write_text(json.dumps(result, indent=2) + "\n")


def process_metrics(pid):
    fields = Path(f"/proc/{pid}/stat").read_text().rsplit(") ", 1)[1].split()
    ticks = int(fields[11]) + int(fields[12])
    status = Path(f"/proc/{pid}/status").read_text().splitlines()
    rss = next(int(line.split()[1]) for line in status if line.startswith("VmRSS:"))
    return ticks / os.sysconf("SC_CLK_TCK"), rss


def server_connections(pid):
    rows = []
    for line in Path(f"/proc/{pid}/net/tcp").read_text().splitlines()[1:]:
        fields = line.split()
        if fields[1].endswith(":1F90") and fields[3] == "01":
            rows.append((int(fields[2].split(":")[1], 16),
                         int(fields[4].split(":")[0], 16)))
    return rows


def stalled_reader():
    sock = socket.socket()
    sock.settimeout(5)
    # Set before connect to keep the negotiated receive window small.
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    try:
        sock.connect(("127.0.0.1", 8080))
        sock.sendall(b"COUNT\n")
        expect(sock, f"{KEYS + 1}\n".encode())
        sock.sendall((b"GET " + SLOW_KEY + b"\n") * 2000)
    except Exception:
        sock.close()
        raise
    # Never read these 16,386,000 bytes of requested responses.
    return sock


def reset_close(sock):
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    sock.close()


def percentile(histogram, percentile_value):
    target = math.ceil(sum(histogram.values()) * percentile_value)
    cumulative = 0
    for microseconds, count in sorted(histogram.items()):
        cumulative += count
        if cumulative >= target:
            return microseconds / 1000
    raise RuntimeError("no measured responses")


def run_case(args, clients, slow, cpus):
    name = f"clients-{clients}-{'stalled' if slow else 'baseline'}"
    case_dir = args.output / name
    case_dir.mkdir()
    processes = []
    slow_socket = None
    server = None
    log = (case_dir / "server.log").open("w")
    try:
        server = subprocess.Popen([str(args.server)], stdout=log, stderr=log,
                                  preexec_fn=lambda: os.sched_setaffinity(0, {cpus[0]}))
        probe = None
        for _ in range(100):
            if server.poll() is not None:
                raise RuntimeError("owned server exited during startup")
            try:
                probe = connect()
                break
            except OSError:
                time.sleep(0.02)
        if probe is None:
            raise RuntimeError("server startup timed out")
        time.sleep(0.05)
        if server.poll() is not None:
            raise RuntimeError("owned server could not bind; port may be occupied")
        with probe:
            for index in range(0, KEYS, 100):
                batch = SETS[index:index + 100]
                probe.sendall(b"".join(batch))
                expect(probe, b"OK\n" * len(batch))
            probe.sendall(b"SET " + SLOW_KEY + b" " + b"s" * 8192 + b"\n")
            expect(probe, b"OK\n")
        time.sleep(0.05)
        if slow:
            slow_socket = stalled_reader()
        # Spawn avoids inheriting the parent's stalled-reader socket; inherited
        # copies would keep old connections alive when the parent renews them.
        context = mp.get_context("spawn")
        begin = context.Value("q", 0)
        go = context.Event()
        ready = context.Queue()
        workers = min(4, clients, len(cpus) - 2)
        for index in range(workers):
            count = clients // workers + (index < clients % workers)
            process = context.Process(target=worker, args=(
                index, count, cpus[index + 1], begin, go, ready,
                args.warmup, args.seconds, str(case_dir / f"worker-{index}.json")))
            process.start()
            processes.append(process)
        for _ in processes:
            _, error = ready.get(timeout=20)
            if error:
                raise RuntimeError(error)
        begin.value = time.monotonic_ns() + 300_000_000
        go.set()
        measure_start = begin.value / 1e9 + args.warmup
        end = measure_start + args.seconds
        next_rotation = time.monotonic() + 20
        rotations = 0
        renewal_gaps = []
        samples = []
        print(f"START {name}: {args.warmup}s warmup + {args.seconds}s measured", flush=True)
        time.sleep(max(0, measure_start - time.monotonic()))
        cpu_before, rss = process_metrics(server.pid)
        sample_begin = time.monotonic()
        next_sample = sample_begin
        while time.monotonic() < end:
            if server.poll() is not None:
                raise RuntimeError("server exited during measurement")
            if slow and time.monotonic() >= next_rotation:
                renewal_start = time.monotonic()
                old_port = slow_socket.getsockname()[1]
                reset_close(slow_socket)
                slow_socket = None
                # Wait for the slot to be retired before taking it again at
                # the 128-client cap. A tiny renewal gap is recorded explicitly.
                deadline = time.monotonic() + 2
                while old_port in dict(server_connections(server.pid)):
                    if time.monotonic() > deadline:
                        raise RuntimeError("stalled connection failed to retire")
                    time.sleep(0.001)
                while slow_socket is None:
                    try:
                        slow_socket = stalled_reader()
                    except (OSError, RuntimeError):
                        if time.monotonic() > deadline:
                            raise
                        time.sleep(0.005)
                renewal_gaps.append(time.monotonic() - renewal_start)
                rotations += 1
                next_rotation = time.monotonic() + 20
            now = time.monotonic()
            if now >= next_sample:
                cpu, rss = process_metrics(server.pid)
                connections = dict(server_connections(server.pid))
                queue = connections.get(slow_socket.getsockname()[1], 0) if slow else 0
                samples.append(dict(elapsed_s=now - measure_start, cpu_seconds=cpu,
                                    rss_kib=rss, stalled_send_queue_bytes=queue,
                                    established_connections=len(connections)))
                if len(samples) % 10 == 0:
                    print(f"  {name}: {len(samples)}s sampled", flush=True)
                next_sample += 1
            time.sleep(max(0, min(0.05, end - time.monotonic())))
        cpu_after, rss_after = process_metrics(server.pid)
        sample_end = time.monotonic()
        for process in processes:
            process.join(timeout=10)
            if process.is_alive() or process.exitcode != 0:
                raise RuntimeError("load worker did not finish successfully")
        results = [json.loads((case_dir / f"worker-{i}.json").read_text())
                   for i in range(workers)]
        for result in results:
            if "error" in result:
                raise RuntimeError(result["error"])
        hist = collections.Counter()
        counts = collections.Counter()
        per_second = [0] * math.ceil(args.seconds)
        for result in results:
            hist.update({int(k): v for k, v in result["histogram_us"].items()})
            counts.update(result["counts"])
            for index, count in enumerate(result["per_second"]):
                per_second[index] += count
        pressure_samples = sum(s["stalled_send_queue_bytes"] > 0 for s in samples)
        if slow and pressure_samples < len(samples) * 0.9:
            raise RuntimeError("stalled reader did not maintain send-queue pressure")
        if sum(counts.values()) != sum(hist.values()) or min(per_second) == 0:
            raise RuntimeError("invalid or interrupted measurement")
        summary = dict(
            case=name, working_clients=clients, stalled_clients=int(slow),
            measured_seconds=args.seconds, warmup_seconds=args.warmup,
            completed_operations=sum(counts.values()), operation_counts=dict(counts),
            operations_per_second=sum(counts.values()) / args.seconds,
            p50_ms=percentile(hist, 0.50), p95_ms=percentile(hist, 0.95),
            p99_ms=percentile(hist, 0.99),
            maximum_ms=max(r["maximum_ns"] for r in results) / 1e6,
            server_cpu_percent=100 * (cpu_after - cpu_before) / (sample_end - sample_begin),
            load_worker_cpu_percent=[100 * r["cpu_seconds"] / args.seconds for r in results],
            sampled_peak_rss_kib=max([rss_after] + [s["rss_kib"] for s in samples]),
            errors=0, per_second_operations=per_second,
            stalled_reader_rotations=rotations, pressure_samples=pressure_samples,
            stalled_renewal_gap_seconds=renewal_gaps,
            sample_count=len(samples), resource_samples=samples,
            server_cpu=cpus[0], worker_cpus=cpus[1:workers + 1])
        (case_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
        print(f"DONE {name}: {summary['operations_per_second']:.0f} ops/s, "
              f"p95 {summary['p95_ms']:.3f} ms, p99 {summary['p99_ms']:.3f} ms, "
              f"server CPU {summary['server_cpu_percent']:.1f}%", flush=True)
        return summary
    finally:
        for process in processes:
            if process.is_alive():
                process.terminate()
                process.join()
        if slow_socket is not None:
            reset_close(slow_socket)
        if server is not None and server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
        log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--warmup", type=float, default=5)
    parser.add_argument("--smoke", action="store_true", help="run only 32-client pair")
    args = parser.parse_args()
    args.server = args.server.resolve()
    if args.seconds < 1 or args.warmup < 0:
        parser.error("seconds must be >= 1 and warmup must be >= 0")
    cpus = sorted(os.sched_getaffinity(0))
    if len(cpus) < 3:
        parser.error("at least three available logical CPUs are required")
    args.output.mkdir(parents=True, exist_ok=False)
    # Reserve a separate CPU for sampling and orchestration.
    os.sched_setaffinity(0, {cpus[-1]})
    metadata = dict(timestamp_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    platform=platform.platform(), python=platform.python_version(),
                    available_cpus=cpus, cpuinfo=Path("/proc/cpuinfo").read_text(),
                    memory=Path("/proc/meminfo").read_text(),
                    server=str(args.server), dataset_keys=KEYS, value_bytes=VALUE_BYTES,
                    server_sha256=hashlib.sha256(args.server.read_bytes()).hexdigest(),
                    harness_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    extra_stall_key_bytes=8192, get_percent=80, set_percent=20,
                    pipeline_depth=1, latency_histogram_resolution_us=1,
                    clock="time.monotonic_ns", duration_s=args.seconds,
                    warmup_s=args.warmup, stalled_renewal_seconds=20,
                    description="closed-loop, loopback, exact reply validation, fixed-seed keys")
    (args.output / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")
    cases = [(32, False), (32, True)] if args.smoke else [
        (1, False), (1, True), (32, False), (32, True),
        (127, False), (127, True), (128, False)]
    summaries = []
    for clients, slow in cases:
        summaries.append(run_case(args, clients, slow, cpus))
        (args.output / "results.json").write_text(json.dumps(summaries, indent=2) + "\n")
    print(f"Results saved to {args.output}", flush=True)


if __name__ == "__main__":
    main()
