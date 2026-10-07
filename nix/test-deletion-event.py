"""Check the packaged Core deletion event contract using a temporary account.

This exercises the local deletion event, shared with received deletion requests;
it does not simulate remote delivery or the Qt UI.
"""
import json, os, pathlib, subprocess, tempfile, select
root = pathlib.Path(__file__).resolve().parents[1] / 'dist/delta-tel/telegram-lib'
with tempfile.TemporaryDirectory(prefix='delta-delete-regression-') as folder:
    env = dict(os.environ, DC_ACCOUNTS_PATH=folder, RUST_LOG='error')
    env.pop('LD_LIBRARY_PATH', None)
    p = subprocess.Popen([str(root/'rpc-rt/ld-linux-x86-64.so.2'), '--library-path', str(root/'rpc-rt'), str(root/'deltachat-rpc-server')], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    seq = 0
    def call(method, params):
        global seq
        seq += 1
        p.stdin.write(json.dumps(dict(jsonrpc='2.0', id=seq, method=method, params=params))+'\n'); p.stdin.flush()
        assert select.select([p.stdout], [], [], 30)[0], 'RPC timeout'
        reply = json.loads(p.stdout.readline())
        assert reply['id'] == seq and 'error' not in reply, reply
        return reply.get('result')
    try:
        account = call('add_account', [])
        msg = call('add_device_message', [account, 'delete-regression', {'text': 'delete regression'}])
        chat = call('get_message', [account, msg])['chatId']
        call('delete_messages', [account, [msg]])
        found = False
        for _ in range(100):
            envelope = call('get_next_event', [])
            event = envelope['event']
            if event['kind'] == 'MsgDeleted':
                assert envelope['contextId'] == account, envelope
                assert event['chatId'] == chat and event['msgId'] == msg, event
                found = True
                break
        assert found, 'Missing deletion event'
        assert msg not in call('get_message_ids', [account, chat, False, False])
        print('PASS: deletion event account/chat/message identifiers and removal from Core history')
    finally:
        p.terminate(); p.wait(timeout=10)
