"""One integration check for the source-driven circuit simulator."""

import importlib.util
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from threading import Thread
from urllib.error import HTTPError
from urllib.request import Request, urlopen


root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("circuit_server", root / "simulation/circuit_server.py")
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)

with TemporaryDirectory() as temp:
    directory = Path(temp)
    server.prepare({}, directory)
    normal = server.simulate({"fault": "none"}, directory)
    assert not normal["issues"] and len(normal["rows"]) == 751

    # A real source regression must change the result, even if the trace still renders.
    header = directory / "pico/flight.hpp"
    original = header.read_text()
    assert original.count("frame_.pressure_pa = reading.pressure_pa;") == 1
    header.write_text(original.replace("frame_.pressure_pa = reading.pressure_pa;", "frame_.pressure_pa = 0;"))
    broken = server.simulate({"fault": "none"}, directory)
    assert any("Pressure sent by firmware differs" in issue for issue in broken["issues"])
    header.write_text(original.replace("frame_.temp_centi_c = reading.temp_centi_c;", "frame_.temp_centi_c = 0;"))
    broken_temp = server.simulate({"fault": "none"}, directory)
    assert any("Temperature sent by firmware differs" in issue for issue in broken_temp["issues"])

httpd = server.ThreadingHTTPServer(("127.0.0.1", 0), server.Handler)
server.PORT = httpd.server_address[1]
thread = Thread(target=httpd.serve_forever, daemon=True)
thread.start()
try:
    for path in ("run", "compile"):
        request = Request(f"http://127.0.0.1:{server.PORT}/api/{path}", b"{}", {"X-Circuit-Token": "stale"}, method="POST")
        try:
            urlopen(request, timeout=5)
            raise AssertionError("Stale page token was accepted")
        except HTTPError as error:
            assert error.code == 409
            assert error.headers.get_content_type() == "application/json"
            assert "Refresh this browser tab" in json.load(error)["error"]
finally:
    httpd.shutdown()
    thread.join()
    httpd.server_close()
