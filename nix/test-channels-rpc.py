"""Exercise channel UI operations against Core, without real accounts or mail IO."""
import json
import os
import pathlib
import select
import sqlite3
import subprocess
import struct
import tempfile
import zlib

root = pathlib.Path(__file__).resolve().parents[1] / 'dist/delta-tel/telegram-lib'
with tempfile.TemporaryDirectory(prefix='delta-channels-regression-') as folder:
    env = dict(os.environ, DC_ACCOUNTS_PATH=folder, RUST_LOG='error')
    env.pop('LD_LIBRARY_PATH', None)
    process = subprocess.Popen(
        [str(root / 'rpc-rt/ld-linux-x86-64.so.2'), '--library-path',
         str(root / 'rpc-rt'), str(root / 'deltachat-rpc-server')],
        env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, text=True)
    sequence = 0

    def call(method, params):
        global sequence
        sequence += 1
        process.stdin.write(json.dumps(dict(jsonrpc='2.0', id=sequence,
                                           method=method, params=params)) + '\n')
        process.stdin.flush()
        assert select.select([process.stdout], [], [], 30)[0], 'RPC timeout'
        reply = json.loads(process.stdout.readline())
        assert reply['id'] == sequence and 'error' not in reply, reply
        return reply.get('result')

    def offline_account(address):
        before = set(pathlib.Path(folder).rglob('dc.db'))
        account = call('add_account', [])
        database, = set(pathlib.Path(folder).rglob('dc.db')) - before
        # Same empty-server transport used by Core's add_pseudo_transport.
        # This is an isolated fixture, not application code. Never start mail IO.
        configured = dict(addr=address, imap=[], smtp=[], imap_user='',
                          smtp_user='', imap_password='', smtp_password='',
                          certificate_checks='Automatic', oauth2=False)
        with sqlite3.connect(database) as db:
            db.execute('INSERT INTO transports(addr, entered_param, configured_param) VALUES(?,?,?)',
                       (address, '{}', json.dumps(configured)))
        call('set_config', [account, 'configured_addr', address])
        return account, database

    try:
        owner, owner_database = offline_account('channel-owner@example.org')
        reader, reader_database = offline_account('channel-reader@example.org')
        channel = call('create_broadcast', [owner, 'Test channel'])
        info = call('get_full_chat_by_id', [owner, channel])
        assert info['chatType'] == 'OutBroadcast' and info['canSend'], info
        assert info['contactIds'] == [], info
        call('set_chat_description', [owner, channel, 'Channel description'])
        assert call('get_chat_description', [owner, channel]) == 'Channel description'
        call('set_chat_name', [owner, channel, 'Renamed channel'])
        image = pathlib.Path(folder) / 'photo.png'
        def png_chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
        image.write_bytes(b'\x89PNG\r\n\x1a\n'
                          + png_chunk(b'IHDR', struct.pack('>IIBBBBB', 32, 32, 8, 2, 0, 0, 0))
                          + png_chunk(b'IDAT', zlib.compress((b'\0' + b'\x40\x90\xe0' * 32) * 32))
                          + png_chunk(b'IEND', b''))
        call('set_chat_profile_image', [owner, channel, str(image)])
        info = call('get_full_chat_by_id', [owner, channel])
        assert info['profileImage'] and pathlib.Path(info['profileImage']).exists(), info
        invite = call('get_chat_securejoin_qr_code', [owner, channel])
        assert invite.startswith('https://i.delta.chat/#') and '&b=Renamed+channel' in invite, invite
        assert '<svg' in call('create_qr_svg', [invite])
        preview = call('check_qr', [reader, invite])
        assert preview['kind'] == 'askJoinBroadcast' and preview['name'] == 'Renamed channel', preview
        # Exchange Core-generated identity cards locally: no mail IO is started.
        call('import_vcard_contents', [reader, call('make_vcard', [owner, [1]])])
        subscribed = call('secure_join', [reader, invite])
        incoming = call('get_full_chat_by_id', [reader, subscribed])
        assert incoming['chatType'] == 'InBroadcast' and not incoming['canSend'], incoming
        assert 1 not in incoming['contactIds'], incoming  # Handshake pending: no Leave action yet.
        for kind, muted in [('Forever', True), ('NotMuted', False)]:
            call('set_chat_mute_duration', [reader, subscribed, dict(kind=kind)])
            assert call('get_full_chat_by_id', [reader, subscribed])['isMuted'] == muted
        # Mail IO is deliberately off. Seed established SELF membership in this
        # disposable fixture to exercise Core's leave operation after a handshake.
        with sqlite3.connect(reader_database) as db:
            db.execute('INSERT OR REPLACE INTO chats_contacts(chat_id, contact_id, add_timestamp, remove_timestamp) VALUES(?,?,?,?)',
                       (subscribed, 1, 1, 0))
        assert 1 in call('get_full_chat_by_id', [reader, subscribed])['contactIds']
        call('remove_contact_from_chat', [reader, subscribed, 1])
        assert 1 not in call('get_full_chat_by_id', [reader, subscribed])['contactIds']
        subscriber, = call('import_vcard_contents', [owner, call('make_vcard', [reader, [1]])])
        with sqlite3.connect(owner_database) as db:
            db.execute('INSERT OR REPLACE INTO chats_contacts(chat_id, contact_id, add_timestamp, remove_timestamp) VALUES(?,?,?,?)',
                       (channel, subscriber, 1, 0))
        assert subscriber in call('get_full_chat_by_id', [owner, channel])['contactIds']
        assert any(row['id'] == subscriber for row in call('get_contacts', [owner, 2, None]))
        post = call('send_msg', [owner, channel, dict(text='View count test')])
        assert call('get_message_read_receipt_count', [owner, post]) == 0
        # Stored receipt fixture; no network MDN delivery is simulated.
        with sqlite3.connect(owner_database) as db:
            db.execute('INSERT INTO msgs_mdns(msg_id,contact_id,timestamp_sent) VALUES(?,?,?)', (post, subscriber, 1))
        assert call('get_message_read_receipt_count', [owner, post]) == 1
        group = call('create_group_chat', [owner, 'Selected contacts group', False])
        call('add_contact_to_chat', [owner, group, subscriber])
        members = call('get_full_chat_by_id', [owner, group])['contactIds']
        assert 1 in members and subscriber in members, members
        group_invite = call('get_chat_securejoin_qr_code', [owner, group])
        group_preview = call('check_qr', [reader, group_invite])
        assert group_preview['kind'] == 'askVerifyGroup' and group_preview['grpname'] == 'Selected contacts group', group_preview
        contact_invite = call('get_chat_securejoin_qr_code', [owner, None])
        contact_preview = call('check_qr', [reader, contact_invite])
        assert contact_preview['kind'] == 'askVerifyContact', contact_preview
        assert call('get_contact', [reader, contact_preview['contact_id']])['displayName']
        call('remove_contact_from_chat', [owner, channel, subscriber])
        assert subscriber not in call('get_full_chat_by_id', [owner, channel])['contactIds']
        print('PASS: channel create/edit/photo/invite/join, read-only, mute/leave, subscriber removal, receipt counts and selected group members')
    finally:
        process.terminate()
        process.wait(timeout=10)
