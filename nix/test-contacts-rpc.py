import base64, json, os, pathlib, subprocess, tempfile, select, sqlite3, struct, zlib
root = pathlib.Path(__file__).resolve().parents[1] / 'dist/delta-tel/telegram-lib'
with tempfile.TemporaryDirectory(prefix='delta-contacts-regression-') as folder:
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
        contact = call('create_contact', [account, 'contacts-test@example.org', 'Contact test'])
        # A known-contact biography fixture. The bundled Core exposes status
        # through get_contact, but does not import NOTE from a vCard.
        database, = pathlib.Path(folder).rglob('dc.db')
        with sqlite3.connect(database) as db:
            db.execute('UPDATE contacts SET status=? WHERE id=?', ('Available for a chat', contact))
        contacts = call('get_contacts', [account, 2, None]) + call('get_contacts', [account, 4, None])
        assert any(row['id'] == contact and row['displayName'] == 'Contact test' for row in contacts), contacts
        chat = call('create_chat_by_contact_id', [account, contact])
        assert chat > 9
        assert call('create_chat_by_contact_id', [account, contact]) == chat
        card = call('make_vcard', [account, [contact]])
        # An attached photo belongs to the shared contact, not the sender.
        def chunk(kind, data):
            return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))
        image = (b'\x89PNG\r\n\x1a\n'
                 + chunk(b'IHDR', struct.pack('!IIBBBBB', 32, 32, 8, 2, 0, 0, 0))
                 + chunk(b'IDAT', zlib.compress((b'\0' + b'\x40\x90\xe0' * 32) * 32))
                 + chunk(b'IEND', b''))
        photo = base64.b64encode(image).decode()
        card = card.replace('END:VCARD', 'PHOTO:data:image/jpeg;base64\\,' + photo + '\r\nEND:VCARD')
        path = pathlib.Path(folder) / 'contact.vcf'
        path.write_text(card)
        parsed = call('parse_vcard', [str(path)])
        assert parsed[0]['addr'] == 'contacts-test@example.org', parsed
        # Core shares the authenticated name, not our private address-book label.
        assert parsed[0]['displayName'], parsed
        assert parsed[0]['profileImage'] == photo, parsed
        message = call('add_device_message', [account, 'contact-card-test', dict(file=str(path), viewtype='Vcard')])
        path.unlink()
        stored = call('get_message', [account, message])
        assert stored['viewType'] == 'Vcard', stored
        assert stored['vcardContact']['addr'] == parsed[0]['addr'], stored
        assert stored['vcardContact']['profileImage'] == photo, stored
        info = call('get_message_info', [account, message])
        assert info.startswith('Sent: ') and 'contact.vcf' in info, info
        imported = call('import_vcard', [account, stored['file']])
        assert imported, imported
        profile = call('get_contact', [account, imported[0]])
        assert profile['displayName'] and 'status' in profile, profile
        assert profile['status'] == 'Available for a chat', profile
        assert profile['profileImage'] and pathlib.Path(profile['profileImage']).exists(), profile
        card_chat = call('create_chat_by_contact_id', [account, imported[0]])
        assert card_chat > 9
        assert call('get_chat_id_by_contact_id', [account, imported[0]]) == card_chat
        keep = call('create_contact', [account, 'keep-chat@example.org', 'Keep chat'])
        keep_chat = call('create_chat_by_contact_id', [account, keep])
        call('delete_contact', [account, keep])
        assert call('get_chat_id_by_contact_id', [account, keep]) == keep_chat
        assert not any(row['id'] == keep for row in call('get_contacts', [account, 4, None]))
        both = call('create_contact', [account, 'delete-both@example.org', 'Delete both'])
        both_chat = call('create_chat_by_contact_id', [account, both])
        assert call('get_chat_id_by_contact_id', [account, both]) == both_chat
        call('delete_chat', [account, both_chat])
        call('delete_contact', [account, both])
        assert call('get_chat_id_by_contact_id', [account, both]) is None
        assert not any(row['id'] == both for row in call('get_contacts', [account, 4, None]))
        absent = call('create_contact', [account, 'no-chat@example.org', 'No chat'])
        assert call('get_chat_id_by_contact_id', [account, absent]) is None
        call('delete_contact', [account, absent])
        print('PASS: contacts listing/opening; Core card generation, parsing, blob persistence and import')
    finally:
        p.terminate(); p.wait(timeout=10)
