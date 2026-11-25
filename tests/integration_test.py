#!/usr/bin/env python3
"""Network-level tests: fragmentation, authorization, integer limits, auction timers."""
import os
import shlex
import socket
import subprocess
import tempfile
import threading
import time
from pathlib import Path

MESSAGE_NAMES = {'100': 'CONNECTED', '110': 'ITEM_STARTED', '111': 'MEMBER_EVENT', '113': 'ITEM_EVENT', '120': 'NEW_BID', '121': 'CLOSING_NOTICE', '130': 'ITEM_SOLD', '131': 'QUEUE_EMPTY', '200': 'OK', '201': 'ROOM_DATA', '202': 'ITEM_DATA', '400': 'INVALID_REQUEST', '401': 'AUTH_ERROR', '403': 'FORBIDDEN', '404': 'NOT_FOUND', '409': 'INVALID_STATE', '422': 'INVALID_DATA', '429': 'LIMIT_REACHED', '501': 'STORAGE_FAILED'}

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('AUCTION_SERVER_BIN', str(ROOT / 'build' / 'auction_server')))

class Peer:
    def __init__(self, port):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=2)
        self.socket.settimeout(3)
        self.pending = bytearray()
        self.expect('100 CONNECTED')

    def send(self, line):
        self.socket.sendall((line + '\r\n').encode('utf-8'))

    def send_raw(self, data):
        self.socket.sendall(data)

    def get(self, timeout=3):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            idx = self.pending.find(b'\n')
            if idx >= 0:
                assert idx > 0 and self.pending[idx-1] == 13, 'Missing CRLF'
                msg = self.pending[:idx-1].decode('utf-8')
                assert len(msg) >= 3 and msg[:3].isdigit() and (len(msg) == 3 or msg[3] == ' '), msg
                fields = shlex.split(msg)
                assert len(fields) >= 2 and MESSAGE_NAMES.get(fields[0]) == fields[1], msg
                if int(fields[0]) >= 400:
                    assert len(fields) == 3 and fields[2], msg
                del self.pending[:idx+1]
                return msg
            self.socket.settimeout(max(0.01, end-time.monotonic()))
            chunk = self.socket.recv(4096)
            if not chunk:
                raise AssertionError('peer unexpectedly closed')
            self.pending += chunk
        raise AssertionError('timeout waiting for message')

    def expect(self, prefix, timeout=3):
        end = time.monotonic() + timeout
        seen = []
        while time.monotonic() < end:
            msg = self.get(end-time.monotonic())
            if msg.startswith(prefix):
                return msg
            seen.append(msg)
        raise AssertionError(f'Expected {prefix}, got {seen}')

    def run(self, cmd, expected='2'):
        self.send(cmd)
        return self.expect(('200 OK ' + cmd.split()[0], '4', '5') if expected == '2' else expected)

    def close(self):
        self.socket.close()

def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

def wait_server(port):
    deadline = time.monotonic()+4
    while time.monotonic()<deadline:
        try:
            with socket.create_connection(('127.0.0.1',port),timeout=0.15):
                return
        except OSError:
            time.sleep(0.05)
    raise AssertionError('server did not start')

def start(port, data_dir):
    p = subprocess.Popen([str(BINARY), '--port', str(port), '--data-dir', data_dir,
                          '--bid-timeout','1', '--notice-gap','1'],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    wait_server(port)
    return p

def main():
    if not os.environ.get('DB_CONN_STR'):
        raise RuntimeError('DB_CONN_STR is required. Run tests/run_postgres_tests.py for an isolated PostgreSQL schema.')
    with tempfile.TemporaryDirectory(prefix='auction-test-') as temp:
        port=free_port()
        server=start(port,temp)
        peers=[]
        try:
            alice,bob,carol,dave=[Peer(port) for _ in range(4)]
            peers=[alice,bob,carol,dave]
            assert alice.run('UNKNOWN', '400 INVALID_REQUEST "Sai tên lệnh hoặc số tham số"') == '400 INVALID_REQUEST "Sai tên lệnh hoặc số tham số"'
            assert alice.run('LOGIN alice', '400 INVALID_REQUEST "Sai tên lệnh hoặc số tham số"') == '400 INVALID_REQUEST "Sai tên lệnh hoặc số tham số"'
            assert alice.run('LOGIN|alice|TestPass123', '400 INVALID_REQUEST "Sai tên lệnh hoặc số tham số"') == '400 INVALID_REQUEST "Sai tên lệnh hoặc số tham số"'
            assert alice.run('CREATE_ROOM "unterminated', '400 INVALID_REQUEST "Sai tên lệnh hoặc số tham số"') == '400 INVALID_REQUEST "Sai tên lệnh hoặc số tham số"'
            alice.send_raw(b'HELP\n')
            assert alice.expect('400 INVALID_REQUEST "Sai ký tự kết thúc dòng"') == '400 INVALID_REQUEST "Sai ký tự kết thúc dòng"'
            alice.send_raw(b'HELP\r')
            alice.send_raw(b'\n')
            assert alice.expect('200 OK HELP ')
            for peer,name in zip(peers,['alice','bob','carol','dave']):
                assert peer.run(f'REGISTER {name} TestPass123') == f'200 OK REGISTER {name}'
                assert peer.run(f'LOGIN {name} TestPass123') == f'200 OK LOGIN {name}'
            assert alice.run('CREATE_ROOM "Đấu giá thiết bị"') == '200 OK CREATE_ROOM 1 "Đấu giá thiết bị"'
            assert alice.run('JOIN_ROOM 1',('4', '5')) == '409 INVALID_STATE "Đang ở trong phòng"'
            assert bob.run('JOIN_ROOM 1') == '200 OK JOIN_ROOM 1 "Đấu giá thiết bị"'
            assert alice.expect('111 MEMBER_EVENT 1 JOIN bob')
            assert carol.run('JOIN_ROOM 1').startswith('200 OK JOIN_ROOM')
            assert dave.run('CREATE_ROOM "Other room"').startswith('200 OK CREATE_ROOM 2')
            assert bob.run('JOIN_ROOM 2',('4', '5')) == '409 INVALID_STATE "Đang ở trong phòng"'
            assert bob.run('ADD_ITEM Bad Test 100000 200000',('4', '5')) == '403 FORBIDDEN "Chỉ chủ phòng được thêm vật phẩm"'
            assert bob.run('START',('4', '5')) == '403 FORBIDDEN "Chỉ chủ phòng được bắt đầu đấu giá"'
            assert alice.run('ADD_ITEM Bad Test 9999999999999999999999 100000',('4', '5')).startswith('422 INVALID_DATA "Giá không hợp lệ"')
            assert alice.run('ADD_ITEM Bad Test 100000 90000',('4', '5')).startswith('422 INVALID_DATA "Giá không hợp lệ"')
            assert alice.run('ADD_ITEM Laptop "Đã sử dụng" 100000 200000').startswith('200 OK ADD_ITEM 1')
            assert alice.run('ADD_ITEM Phone New 50000 90000').startswith('200 OK ADD_ITEM 2')
            assert alice.run('WITHDRAW_ITEM 2') == '200 OK WITHDRAW_ITEM 2'
            assert bob.expect('113 ITEM_EVENT 1 ADD 1 Laptop')
            assert bob.expect('113 ITEM_EVENT 1 WITHDRAW 2')
            assert alice.run('ADD_ITEM Mouse USB 10000 50000').startswith('200 OK ADD_ITEM 3')
            assert alice.run('START') == '200 OK START'
            assert alice.run('WITHDRAW_ITEM 1',('4', '5')) == '409 INVALID_STATE "Vật phẩm không ở trạng thái chờ"'
            assert bob.run('BID 105000',('4', '5')).startswith('422 INVALID_DATA "Giá đặt không hợp lệ"')
            assert bob.run('BID 9223372036854775808',('4', '5')).startswith('422 INVALID_DATA "Giá đặt không hợp lệ"')
            assert alice.run('BID 110000',('4', '5')) == '403 FORBIDDEN "Người bán không được tự đặt giá"'
            bob.send_raw(b'BID 11')
            time.sleep(0.02)
            bob.send_raw(b'0000\r\n')
            assert bob.expect('200 OK BID 1 110000') == '200 OK BID 1 110000'
            assert carol.run('BID 120000') == '200 OK BID 1 120000'
            # Wait through 3 notifications; winner must be most recent highest bidder.
            assert alice.expect('121 CLOSING_NOTICE 1 1 1/3', timeout=3)
            assert alice.expect('121 CLOSING_NOTICE 1 1 2/3', timeout=3)
            assert alice.expect('121 CLOSING_NOTICE 1 1 3/3', timeout=3)
            assert alice.expect('130 ITEM_SOLD 1 1 carol 120000 BID',timeout=2)
            assert alice.expect('110 ITEM_STARTED 1 3',timeout=2)
            assert bob.run('BUY_NOW') == '200 OK BUY_NOW 3 50000'
            assert alice.expect('130 ITEM_SOLD 1 3 bob 50000 BUY_NOW',timeout=2)
            assert bob.run('BID 15000',('4', '5')) == '409 INVALID_STATE "Không có vật phẩm đang đấu giá"'
            assert bob.run('LIST_ITEMS').startswith('200 OK LIST_ITEMS 3 ACTIVE=0')
            assert alice.run('LEAVE_ROOM') == '200 OK LEAVE_ROOM'
            assert alice.run('CREATE_ROOM "New room"') == '200 OK CREATE_ROOM 3 "New room"'
            assert alice.run('ADD_ITEM ResetTest Monitor 100000 250000') == '200 OK ADD_ITEM 4'
            assert alice.run('START') == '200 OK START'
            assert bob.run('LEAVE_ROOM') == '200 OK LEAVE_ROOM'
            assert bob.run('JOIN_ROOM 3') == '200 OK JOIN_ROOM 3 "New room"'
            assert carol.run('LEAVE_ROOM') == '200 OK LEAVE_ROOM'
            assert carol.run('JOIN_ROOM 3') == '200 OK JOIN_ROOM 3 "New room"'
            assert bob.run('BID 110000') == '200 OK BID 4 110000'
            assert alice.expect('121 CLOSING_NOTICE 3 4 1/3', timeout=3)
            assert carol.run('BID 120000') == '200 OK BID 4 120000'
            # The new bid cancels the old pending 2/3 and restarts from 1/3.
            assert alice.expect('121 CLOSING_NOTICE 3 4 1/3', timeout=3)
            assert alice.expect('121 CLOSING_NOTICE 3 4 2/3', timeout=3)
            assert alice.expect('121 CLOSING_NOTICE 3 4 3/3', timeout=3)
            assert alice.expect('130 ITEM_SOLD 3 4 carol 120000 BID', timeout=2)
            assert alice.run('ADD_ITEM RaceBuy Phone 50000 100000') == '200 OK ADD_ITEM 5'
            assert alice.run('START') == '200 OK START'
            outcomes = []
            def buy(peer):
                peer.send('BUY_NOW')
                while True:
                    reply = peer.get()
                    if reply.startswith('200 OK BUY_NOW') or reply.startswith('409 INVALID_STATE "Không có vật phẩm đang đấu giá"'):
                        outcomes.append(reply)
                        return
            x = threading.Thread(target=buy, args=(bob,))
            y = threading.Thread(target=buy, args=(carol,))
            x.start(); y.start(); x.join(timeout=4); y.join(timeout=4)
            assert len(outcomes) == 2, outcomes
            assert sum(v.startswith('200 OK BUY_NOW 5 100000') for v in outcomes) == 1, outcomes
            assert sum(v.startswith('409 INVALID_STATE "Không có vật phẩm đang đấu giá"') for v in outcomes) == 1, outcomes
            assert alice.expect('130 ITEM_SOLD 3 5 ', timeout=2)
            assert alice.run('LOGOUT') == '200 OK LOGOUT'
            assert alice.run('LOGIN alice WrongPass',('4', '5')) == '401 AUTH_ERROR "Sai tài khoản hoặc mật khẩu"'
            assert alice.run('LOGIN alice TestPass123') == '200 OK LOGIN alice'
            assert bob.run('LIST_ROOMS').startswith('200 OK LIST_ROOMS 3')
            # Removed commands must be rejected even for logged-in clients.
            assert bob.run('WHOAMI', '400 INVALID_REQUEST "Sai tên lệnh hoặc số tham số"') == '400 INVALID_REQUEST "Sai tên lệnh hoặc số tham số"'
            assert bob.run('CHANGE_PASSWORD TestPass123 AnotherPass123', '400 INVALID_REQUEST "Sai tên lệnh hoặc số tham số"') == '400 INVALID_REQUEST "Sai tên lệnh hoặc số tham số"'
            # Stream coalescing of multiple commands; consume each list's rows.
            bob.send_raw(b'LIST_ROOMS\r\nLIST_ROOMS\r\n')
            for _ in range(2):
                assert bob.expect('200 OK LIST_ROOMS ') == '200 OK LIST_ROOMS 3'
                for _ in range(3):
                    assert bob.expect('201 ROOM_DATA LIST_ROOMS ')
            # oversized line: error and graceful disconnection
            intruder = Peer(port)
            intruder.send_raw(b'X'*2500+b'\r\n')
            assert intruder.expect('400 INVALID_REQUEST "Message không hợp lệ hoặc quá dài"').startswith('400 INVALID_REQUEST "Message không hợp lệ hoặc quá dài"')
            intruder.close()
            # 1 online account cannot log in again concurrently
            intruder = Peer(port)
            assert intruder.run('LOGIN bob TestPass123',('4', '5')) == '401 AUTH_ERROR "Tài khoản đang đăng nhập nơi khác"'
            intruder.close()
            print('PASS: register/login, room permissions, FIFO, withdrawal, bids, 3 notices, buy now, TCP framing, bounds')
        finally:
            for peer in peers: peer.close()
            server.terminate()
            try: out=server.communicate(timeout=2)[0]
            except subprocess.TimeoutExpired:
                server.kill(); out=server.communicate()[0]
            if server.returncode not in (-15,0): print('Server output:',out)
        # PostgreSQL now stores the data; TSV account persistence must not be used.
        assert not (Path(temp)/'accounts.tsv').exists()
        logfile=(Path(temp)/'activity.log').read_text()
        assert 'BID' in logfile and 'SOLD' in logfile and 'TestPass123' not in logfile
        server=start(port,temp)
        try:
            user=Peer(port)
            assert user.run('LOGIN bob WrongPass',('4', '5')) == '401 AUTH_ERROR "Sai tài khoản hoặc mật khẩu"'
            assert user.run('LOGIN bob TestPass123') == '200 OK LOGIN bob'
            assert user.run('LIST_ROOMS') == '200 OK LIST_ROOMS 3' # rooms survive restart
            assert user.run('JOIN_ROOM 3') == '200 OK JOIN_ROOM 3 "New room"'
            assert user.run('LIST_ITEMS') == '200 OK LIST_ITEMS 2 ACTIVE=0'
            assert user.expect('202 ITEM_DATA LIST_ITEMS 4 SOLD ResetTest Monitor 100000 250000 120000 carol carol')
            assert user.run('EXIT') == '200 OK EXIT'
            user.close()
        finally:
            server.terminate(); server.communicate(timeout=2)
        print('PASS: PostgreSQL accounts, rooms, items survive restart; audit file contains no passwords')

if __name__ == '__main__': main()
