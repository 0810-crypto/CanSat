"""Local circuit view: compile the shared C++ flight code and replay its output."""

import csv
import io
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import tempfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


ROOT = Path(__file__).resolve().parents[1]
PORT = int(os.environ.get("CANSAT_CIRCUIT_PORT", "0"))
TOKEN = secrets.token_urlsafe(24)
SOURCES = {
    "pico/cansat.hpp",
    "pico/flight.hpp",
    "pico/pins.hpp",
    "pico/firmware.cpp",
    "CMakeLists.txt",
}
PIN_NAMES = ("BMP_SDA", "BMP_SCL", "RF_MISO", "RF_CS", "RF_SCK", "RF_MOSI", "RF_RST")


def prepare(files, directory):
    if not isinstance(files, dict) or set(files) - SOURCES:
        raise ValueError("Upload only the CanSat C++ files shown in the file list.")
    for name in SOURCES:
        content = files.get(name)
        if content is not None and (not isinstance(content, str) or len(content) > 200_000):
            raise ValueError(f"{name} is not a small text source file.")
        target = directory / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if content is None:
            shutil.copy2(ROOT / name, target)
        else:
            target.write_text(content)


def pins_from(header):
    text = header.read_text()
    pins = {}
    for name in PIN_NAMES:
        match = re.search(rf"\b{name}\s*=\s*(\d+)\b", text)
        if not match:
            raise ValueError(f"Cannot read {name} from pico/pins.hpp.")
        pins[name] = int(match.group(1))
    if any(pin > 29 for pin in pins.values()) or len(set(pins.values())) != len(pins):
        raise ValueError("Pin plan contains an out-of-range or repeated GPIO number.")
    return pins


def run_command(args, cwd, timeout):
    try:
        result = subprocess.run(args, cwd=cwd, text=True, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        raise ValueError(f"Build or simulation timed out after {timeout} seconds.") from error
    if result.returncode:
        raise ValueError((result.stderr or result.stdout or "Command failed.")[-6000:])
    return result.stdout


def simulate(payload, directory):
    peak = int(payload.get("peak", 100))
    ambient = int(payload.get("ambient", 20))
    base_pressure = int(payload.get("pressure", 101325))
    fault = payload.get("fault", "none")
    if not 15 <= peak <= 250 or not -20 <= ambient <= 45 or not 90_000 <= base_pressure <= 105_000:
        raise ValueError("Launch settings are outside the supported range.")
    if fault not in ("none", "bmp", "crc", "drop", "tx"):
        raise ValueError("Unknown fault scenario.")
    executable = directory / "circuit_harness"
    run_command([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-pedantic", "-I", str(directory / "pico"), str(ROOT / "simulation/circuit_harness.cpp"), "-o", str(executable)], directory, 20)
    trace = run_command([str(executable), str(peak * 100), str(ambient * 100), str(base_pressure), fault], directory, 10)
    rows = [{key: int(value) for key, value in row.items()} for row in csv.DictReader(io.StringIO(trace))]
    if len(rows) != 751:
        raise ValueError(f"Simulator produced {len(rows)} steps; expected 751.")
    events = [row["frame_event"] for row in rows if row["frame_event"]]
    issues = []
    if events != [1, 2, 3, 4, 5, 6]:
        issues.append(f"Mission events are {events}; expected boot, ready, ascent, descent, landed, recovery.")
    for row in rows:
        if row["bmp_input_ok"] and row["frame_pressure_pa"] != row["sensor_pressure_pa"]:
            issues.append(f"Pressure sent by firmware differs from the sensor at {row['t_ms']} ms.")
            break
    for row in rows:
        if row["bmp_input_ok"] and row["frame_temp_centi"] != row["sensor_temp_centi"]:
            issues.append(f"Temperature sent by firmware differs from the sensor at {row['t_ms']} ms.")
            break
    for row in rows:
        if row["bmp_input_ok"] and row["t_ms"] > 12_000 and abs(row["frame_alt_cm"] - row["physical_alt_cm"]) > 60:
            issues.append(f"Calculated altitude differs from the launch model at {row['t_ms']} ms.")
            break
    for row in rows:
        if not row["bmp_input_ok"] and (row["frame_flags"] & 1 or not row["frame_health"] & 1):
            issues.append(f"BMP failure was not reported at {row['t_ms']} ms.")
            break
    if fault == "tx" and not any(row["frame_flags"] & 2 for row in rows):
        issues.append("The radio timeout was not reported in the next packet.")
    if fault in ("crc", "drop", "tx") and all(row["rx_ok"] for row in rows):
        issues.append("The radio fault did not remove any ground packets.")
    pins = pins_from(directory / "pico/pins.hpp")
    return {"rows": rows, "issues": issues, "pins": pins, "board": payload.get("board", "pico2"), "fault": fault, "source": sorted(payload.get("files", {}))}


def compile_firmware(payload, directory):
    board = payload.get("board", "pico2")
    if board not in ("pico", "pico2"):
        raise ValueError("Choose Pico or Pico 2.")
    sdk = os.environ.get("PICO_SDK_PATH", str(Path.home() / "pico-sdk"))
    toolchain = os.environ.get("PICO_TOOLCHAIN_PATH", str(Path.home() / "arm-toolchain-local"))
    if not Path(sdk).is_dir() or not Path(toolchain).is_dir():
        raise ValueError("Pico SDK or ARM toolchain is missing on this Mac.")
    build = directory / "build"
    run_command(["cmake", "-S", str(directory), "-B", str(build), f"-DPICO_SDK_PATH={sdk}", f"-DPICO_TOOLCHAIN_PATH={toolchain}", f"-DPICO_BOARD={board}"], directory, 90)
    targets = ["cansat_flight", "cansat_ground"]
    output = run_command(["cmake", "--build", str(build), "--target", *targets, "--parallel", "8"], directory, 120)
    return {"ok": True, "board": board, "message": "Flight and ground Pico firmware compiled from the selected files.", "build_tail": output[-1200:]}


class Handler(BaseHTTPRequestHandler):
    def send_json(self, status, body):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.headers.get("Host") != f"127.0.0.1:{PORT}":
            self.send_error(403)
            return
        if self.path != "/":
            self.send_error(404)
            return
        data = (ROOT / "simulation/circuit.html").read_text().replace("__CIRCUIT_TOKEN__", TOKEN).encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        if self.headers.get("Host") != f"127.0.0.1:{PORT}":
            self.send_json(403, {"error": "Request must come from this local circuit lab."})
            return
        if self.path not in ("/api/run", "/api/compile"):
            self.send_json(404, {"error": "Unknown circuit lab action."})
            return
        if self.headers.get("X-Circuit-Token") != TOKEN:
            self.send_json(409, {"error": "The circuit lab restarted. Refresh this browser tab and try again."})
            return
        origin = self.headers.get("Origin")
        if origin and origin != f"http://127.0.0.1:{PORT}":
            self.send_json(403, {"error": "Request must come from this local circuit lab."})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 1_000_000:
                raise ValueError("Request is too large.")
            payload = json.loads(self.rfile.read(length))
            if not isinstance(payload, dict):
                raise ValueError("Expected a project request.")
            with tempfile.TemporaryDirectory(prefix="cansat-circuit-") as temp:
                directory = Path(temp)
                prepare(payload.get("files", {}), directory)
                result = simulate(payload, directory) if self.path == "/api/run" else compile_firmware(payload, directory)
            self.send_json(200, result)
        except (ValueError, TypeError, KeyError, UnicodeError, OSError) as error:
            self.send_json(400, {"error": str(error)})


if __name__ == "__main__":
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    PORT = server.server_address[1]
    print(f"CanSat circuit lab: http://127.0.0.1:{PORT}", flush=True)
    server.serve_forever()
