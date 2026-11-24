#!/usr/bin/env python3
"""Network-level tests: fragmentation, authorization, integer limits, auction timers."""
import os
import socket
import subprocess
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('AUCTION_SERVER_BIN', str(ROOT / 'build' / 'auction_server')))

class Peer:
    def __init__(self, port):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=2)
        self.socket.settimeout(3)
        self.pending = bytearray()
        self.expect('100')

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

    def run(self, cmd, expected='1'):
        self.send(cmd)
        return self.expect(expected)

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
            assert alice.run('UNKNOWN', '300') == '300'
            assert alice.run('LOGIN alice', '300') == '300'
            assert alice.run('LOGIN|alice|TestPass123', '300') == '300'
            assert alice.run('CREATE_ROOM "unterminated', '300') == '300'
            alice.send_raw(b'HELP\n')
            assert alice.expect('303') == '303'
            alice.send_raw(b'HELP\r')
            alice.send_raw(b'\n')
            assert alice.expect('190 ')
            for peer,name in zip(peers,['alice','bob','carol','dave']):
                assert peer.run(f'REGISTER {name} TestPass123') == f'111 {name}'
                assert peer.run(f'LOGIN {name} TestPass123') == f'110 {name}'
            assert alice.run('CREATE_ROOM "Đấu giá thiết bị"') == '140 1 "Đấu giá thiết bị"'
            assert alice.run('JOIN_ROOM 1',('2', '3', '5')) == '231'
            assert bob.run('JOIN_ROOM 1') == '141 1 "Đấu giá thiết bị"'
            assert carol.run('JOIN_ROOM 1').startswith('141')
            assert dave.run('CREATE_ROOM "Other room"').startswith('140 2')
            assert bob.run('JOIN_ROOM 2',('2', '3', '5')) == '231'
            assert bob.run('ADD_ITEM Bad Test 100000 200000',('2', '3', '5')) == '222'
            assert bob.run('START',('2', '3', '5')) == '224'
            assert alice.run('ADD_ITEM Bad Test 9999999999999999999999 100000',('2', '3', '5')).startswith('308')
            assert alice.run('ADD_ITEM Bad Test 100000 90000',('2', '3', '5')).startswith('308')
            assert alice.run('ADD_ITEM Laptop "Đã sử dụng" 100000 200000').startswith('150 1')
            assert alice.run('ADD_ITEM Phone New 50000 90000').startswith('150 2')
            assert alice.run('WITHDRAW_ITEM 2') == '151 2'
            assert alice.run('ADD_ITEM Mouse USB 10000 50000').startswith('150 3')
            assert alice.run('START') == '160'
            assert alice.run('WITHDRAW_ITEM 1',('2', '3', '5')) == '244'
            assert bob.run('BID 105000',('2', '3', '5')).startswith('242')
            assert bob.run('BID 9223372036854775808',('2', '3', '5')).startswith('242')
            assert alice.run('BID 110000',('2', '3', '5')) == '225'
            bob.send_raw(b'BID 11')
            time.sleep(0.02)
            bob.send_raw(b'0000\r\n')
            assert bob.expect('161 1 110000') == '161 1 110000'
            assert carol.run('BID 120000') == '161 1 120000'
            # Wait through 3 notifications; winner must be most recent highest bidder.
            assert alice.expect('411 1 1 1/3', timeout=3)
            assert alice.expect('411 1 1 2/3', timeout=3)
            assert alice.expect('411 1 1 3/3', timeout=3)
            assert alice.expect('420 1 1 carol 120000 BID',timeout=2)
            assert alice.expect('400 1 3',timeout=2)
            assert bob.run('BUY_NOW') == '162 3 50000'
            assert alice.expect('420 1 3 bob 50000 BUY_NOW',timeout=2)
            assert bob.run('BID 15000',('2', '3', '5')) == '241'
            assert bob.run('LIST_ITEMS').startswith('152 3 ACTIVE=0')
            assert alice.run('LEAVE_ROOM') == '142'
            assert alice.run('CREATE_ROOM "New room"') == '140 3 "New room"'
            assert alice.run('ADD_ITEM ResetTest Monitor 100000 250000') == '150 4'
            assert alice.run('START') == '160'
            assert bob.run('LEAVE_ROOM') == '142'
            assert bob.run('JOIN_ROOM 3') == '141 3 "New room"'
            assert carol.run('LEAVE_ROOM') == '142'
            assert carol.run('JOIN_ROOM 3') == '141 3 "New room"'
            assert bob.run('BID 110000') == '161 4 110000'
            assert alice.expect('411 3 4 1/3', timeout=3)
            assert carol.run('BID 120000') == '161 4 120000'
            # The new bid cancels the old pending 2/3 and restarts from 1/3.
            assert alice.expect('411 3 4 1/3', timeout=3)
            assert alice.expect('411 3 4 2/3', timeout=3)
            assert alice.expect('411 3 4 3/3', timeout=3)
            assert alice.expect('420 3 4 carol 120000 BID', timeout=2)
            assert alice.run('ADD_ITEM RaceBuy Phone 50000 100000') == '150 5'
            assert alice.run('START') == '160'
            outcomes = []
            def buy(peer):
                peer.send('BUY_NOW')
                while True:
                    reply = peer.get()
                    if reply.startswith('162') or reply.startswith('241'):
                        outcomes.append(reply)
                        return
            x = threading.Thread(target=buy, args=(bob,))
            y = threading.Thread(target=buy, args=(carol,))
            x.start(); y.start(); x.join(timeout=4); y.join(timeout=4)
            assert len(outcomes) == 2, outcomes
            assert sum(v.startswith('162 5 100000') for v in outcomes) == 1, outcomes
            assert sum(v.startswith('241') for v in outcomes) == 1, outcomes
            assert alice.expect('420 3 5 ', timeout=2)
            assert alice.run('LOGOUT') == '130'
            assert alice.run('LOGIN alice WrongPass',('2', '3', '5')) == '210'
            assert alice.run('LOGIN alice TestPass123') == '110 alice'
            assert bob.run('LIST_ROOMS').startswith('143 3')
            assert bob.run('CHANGE_PASSWORD BadOldPwd AnotherPass123',('2', '3', '5')) == '216'
            assert bob.run('CHANGE_PASSWORD TestPass123 AnotherPass123') == '112'
            # Stream coalescing of multiple commands.
            bob.send_raw(b'WHOAMI\r\nWHOAMI\r\n')
            assert bob.expect('113 bob 3') == '113 bob 3'
            assert bob.expect('113 bob 3') == '113 bob 3'
            # oversized line: error and graceful disconnection
            intruder = Peer(port)
            intruder.send_raw(b'X'*2500+b'\r\n')
            assert intruder.expect('302').startswith('302')
            intruder.close()
            # 1 online account cannot log in again concurrently
            intruder = Peer(port)
            assert intruder.run('LOGIN bob AnotherPass123',('2', '3', '5')) == '214'
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
            assert user.run('LOGIN bob TestPass123',('2', '3', '5')) == '210'
            assert user.run('LOGIN bob AnotherPass123') == '110 bob'
            assert user.run('LIST_ROOMS') == '143 3' # rooms survive restart
            assert user.run('JOIN_ROOM 3') == '141 3 "New room"'
            assert user.run('LIST_ITEMS') == '152 2 ACTIVE=0'
            assert user.expect('153 4 SOLD ResetTest Monitor 100000 250000 120000 carol carol')
            user.close()
        finally:
            server.terminate(); server.communicate(timeout=2)
        print('PASS: PostgreSQL accounts, rooms, items survive restart; audit file contains no passwords')

if __name__ == '__main__': main()
