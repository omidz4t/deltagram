"""Check the packaged RPC server without network IO and bound its lifetime."""
import json
import os
import select
import subprocess
import time

process = subprocess.Popen([os.environ['BUNDLE'], '--bundle-rpc'],
                           stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                           text=True, env={**os.environ, 'DC_ACCOUNTS_PATH': '/tmp/accounts'})
try:
    process.stdin.write(json.dumps({'jsonrpc': '2.0', 'id': 1,
                                   'method': 'get_system_info', 'params': []}) + '\n')
    process.stdin.flush()
    deadline = time.monotonic() + 60
    verified = False
    while time.monotonic() < deadline:
        if not select.select([process.stdout], [], [], max(0, deadline - time.monotonic()))[0]:
            break
        line = process.stdout.readline()
        if not line:
            raise SystemExit('Packaged Core exited before replying')
        response = json.loads(line)
        if response.get('id') == 1:
            assert 'result' in response, response
            print('Packaged Core RPC response passed')
            verified = True
            break
    if not verified:
        raise SystemExit('Packaged Core RPC response timed out')
finally:
    process.stdin.close()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
