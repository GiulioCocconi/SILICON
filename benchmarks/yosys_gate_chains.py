#!/usr/bin/env python3
"""Measure maximal-chain folding, including input parsing and process startup.

Usage: python3 benchmarks/yosys_gate_chains.py build/yosys/silicon_bmux_case.so
Requires yosys on PATH. Each size is tested in both cell insertion orders.
"""
import json
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import time

plugin = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="silicon_gate_scaling_") as directory:
    source = Path(directory) / "chain.il"
    for count in (1000, 2000, 4000, 8000, 16000):
        for reverse in (False, True):
            cells = []
            for i in range(count):
                cells.append(f"""  cell $and \\g{i:06}
    parameter \\A_WIDTH 1
    parameter \\B_WIDTH 1
    parameter \\Y_WIDTH 1
    parameter \\A_SIGNED 0
    parameter \\B_SIGNED 0
    connect \\A \\a [0]
    connect \\B \\{'a' if i == 0 else 'n'} [{i + 1 if i == 0 else i - 1}]
    connect \\Y \\n [{i}]
  end
""")
            if reverse:
                cells.reverse()
            source.write_text(f"module \\top\n  wire width {count + 1} input 1 \\a\n"
                              f"  wire width {count} \\n\n  wire output 2 \\y\n"
                              + "".join(cells) + f"  connect \\y \\n [{count - 1}]\nend\n")
            samples = []
            for _ in range(3):
                started = time.perf_counter()
                subprocess.run(["yosys", "-Q", "-T", "-q", "-m", str(plugin), "-p",
                                f'read_rtlil "{source}"; silicon_gates'], check=True)
                samples.append(time.perf_counter() - started)
            print(json.dumps({"gates": count, "reverse": reverse,
                              "seconds": round(statistics.median(samples), 6)}), flush=True)
