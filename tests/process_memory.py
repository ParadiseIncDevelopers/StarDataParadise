"""Measure memory counters for a test-owned Windows child process."""

import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import subprocess
import threading
import time


POLL_SECONDS = 0.025


class ProcessMemoryCounters(ctypes.Structure):
    _fields_ = [
        ("size", wintypes.DWORD),
        ("page_fault_count", wintypes.DWORD),
        ("peak_working_set", ctypes.c_size_t),
        ("working_set", ctypes.c_size_t),
        ("peak_paged_pool", ctypes.c_size_t),
        ("paged_pool", ctypes.c_size_t),
        ("peak_nonpaged_pool", ctypes.c_size_t),
        ("nonpaged_pool", ctypes.c_size_t),
        ("pagefile_usage", ctypes.c_size_t),
        ("peak_pagefile_usage", ctypes.c_size_t),
    ]


def run_measured(command, password, output, *, cwd):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel.CloseHandle.restype = wintypes.BOOL
    memory = kernel.K32GetProcessMemoryInfo
    memory.argtypes = (wintypes.HANDLE, ctypes.POINTER(ProcessMemoryCounters), wintypes.DWORD)
    memory.restype = wintypes.BOOL

    started = time.perf_counter()
    with subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, encoding="utf-8", errors="replace", cwd=cwd) as process:
        metrics = {"process": Path(command[0]).name, "sample_interval_ms": POLL_SECONDS * 1000,
                   "samples": 0, "peak_working_set_bytes": 0, "max_sampled_working_set_bytes": 0}
        stop = threading.Event()

        def sample():
            counters = ProcessMemoryCounters()
            counters.size = ctypes.sizeof(counters)
            while not stop.is_set():
                if memory(handle, ctypes.byref(counters), counters.size):
                    metrics["samples"] += 1
                    metrics["peak_working_set_bytes"] = max(metrics["peak_working_set_bytes"], counters.peak_working_set)
                    metrics["max_sampled_working_set_bytes"] = max(metrics["max_sampled_working_set_bytes"], counters.working_set)
                stop.wait(POLL_SECONDS)

        # QUERY_INFORMATION + VM_READ for this test's freshly spawned child only.
        handle = kernel.OpenProcess(0x0400 | 0x0010, False, process.pid)
        if not handle:
            process.kill()
            process.communicate(timeout=5)
            raise ctypes.WinError(ctypes.get_last_error())
        sampler = threading.Thread(target=sample)
        sampler.start()
        try:
            stdout, stderr = process.communicate(password + "\n", timeout=90)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate(timeout=10)
            raise RuntimeError("measured Workbench acceptance timed out") from None
        finally:
            stop.set()
            sampler.join()
            kernel.CloseHandle(handle)
        elapsed = time.perf_counter() - started
        if password in stdout or password in stderr:
            raise RuntimeError("fixture credential appeared in process output")
        if process.returncode:
            raise RuntimeError(f"{Path(command[0]).name} failed with exit code {process.returncode}")
        if not metrics["samples"] or not metrics["peak_working_set_bytes"]:
            raise RuntimeError("no working-set observations for Workbench acceptance")
        metrics["elapsed_seconds"] = elapsed
        metrics["exit_code"] = process.returncode
    output.write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
    return stdout