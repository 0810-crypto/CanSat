"""Exercise the real launcher with a pseudo-terminal, including a stalled link."""
import os
from pathlib import Path
import pty
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
master, slave = pty.openpty()
process = subprocess.Popen(['./run', '--port', os.ttyname(slave)], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
try:
    prefix = ''
    while 'WAITING FOR DATA' not in prefix:
        line = process.stdout.readline()
        assert line, prefix
        prefix += line
    os.write(master, b'# test_receiver\nnot telemetry\n100,2,2,2145,100123,0,1,0,3\n')
    time.sleep(3.5)
    os.write(master, b'200,3,2,' + b'0'*150 + b'2146,100120,10,1,0,0\n')
    time.sleep(0.3)
    process.send_signal(signal.SIGINT)
    output = prefix + process.communicate(timeout=5)[0]
    assert process.returncode == 0, output
    assert 'DATA STOPPED' in output and output.count('RECEIVING') == 2, output
    assert '# test_receiver' in output and '21.45 C' in output, output
    log = Path(prefix.split('Log: ')[1].splitlines()[0])
    assert len(log.read_text().splitlines()) == 3, log.read_text()
    print('PASS: USB telemetry, logging, stale link, recovery, and Ctrl+C')
finally:
    if process.poll() is None:
        process.kill()
        process.wait()
    os.close(master)
    os.close(slave)
