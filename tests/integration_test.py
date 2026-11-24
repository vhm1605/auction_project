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
                assert len(msg) >= 5 and msg[:3].isdigit() and msg[3] == ' ', msg
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
            assert alice.run('UNKNOWN', '300 ') == '300 UNKNOWN_COMMAND_OR_WRONG_ARGUMENTS'
            assert alice.run('LOGIN|alice', '300 ') == '300 UNKNOWN_COMMAND_OR_WRONG_ARGUMENTS'
            alice.send_raw(b'HELP\n')
            assert alice.expect('303 ') == '303 INVALID_LINE_ENDING'
            alice.send_raw(b'HELP\r')
            alice.send_raw(b'\n')
            assert alice.expect('190 COMMANDS|')
            for peer,name in zip(peers,['alice','bob','carol','dave']):
                assert peer.run(f'REGISTER|{name}|TestPass123') == f'111 REGISTERED|{name}'
                assert peer.run(f'LOGIN|{name}|TestPass123') == f'110 LOGGED_IN|{name}'
            assert alice.run('CREATE_ROOM|Đấu giá thiết bị') == '140 ROOM_CREATED|1|Đấu giá thiết bị'
            assert alice.run('JOIN_ROOM|1',('2', '3', '5')) == '231 ALREADY_IN_A_ROOM'
            assert bob.run('JOIN_ROOM|1') == '141 JOINED_ROOM|1|Đấu giá thiết bị'
            assert carol.run('JOIN_ROOM|1').startswith('141 JOINED_ROOM')
            assert dave.run('CREATE_ROOM|Other room').startswith('140 ROOM_CREATED|2')
            assert bob.run('JOIN_ROOM|2',('2', '3', '5')) == '231 ALREADY_IN_A_ROOM'
            assert bob.run('ADD_ITEM|Bad|Test|100000|200000',('2', '3', '5')) == '222 ONLY_OWNER_CAN_SELL'
            assert bob.run('START',('2', '3', '5')) == '224 ONLY_OWNER_CAN_START'
            assert alice.run('ADD_ITEM|Bad|Test|9999999999999999999999|100000',('2', '3', '5')).startswith('308 INVALID_PRICE')
            assert alice.run('ADD_ITEM|Bad|Test|100000|90000',('2', '3', '5')).startswith('308 INVALID_PRICE')
            assert alice.run('ADD_ITEM|Laptop|Đã sử dụng|100000|200000').startswith('150 ITEM_QUEUED|1')
            assert alice.run('ADD_ITEM|Phone|New|50000|90000').startswith('150 ITEM_QUEUED|2')
            assert alice.run('WITHDRAW_ITEM|2') == '151 ITEM_WITHDRAWN|2'
            assert alice.run('ADD_ITEM|Mouse|USB|10000|50000').startswith('150 ITEM_QUEUED|3')
            assert alice.run('START') == '160 AUCTION_STARTED'
            assert alice.run('WITHDRAW_ITEM|1',('2', '3', '5')) == '244 ONLY_QUEUED_ITEMS_CAN_BE_WITHDRAWN'
            assert bob.run('BID|105000',('2', '3', '5')).startswith('242 BID_INVALID')
            assert bob.run('BID|9223372036854775808',('2', '3', '5')).startswith('242 BID_INVALID')
            assert alice.run('BID|110000',('2', '3', '5')) == '225 SELLER_CANNOT_BID'
            bob.send_raw(b'BID|11')
            time.sleep(0.02)
            bob.send_raw(b'0000\r\n')
            assert bob.expect('161 BID_ACCEPTED|1|110000') == '161 BID_ACCEPTED|1|110000'
            assert carol.run('BID|120000') == '161 BID_ACCEPTED|1|120000'
            # Wait through 3 notifications; winner must be most recent highest bidder.
            assert alice.expect('411 NOTICE|1|1|1/3', timeout=3)
            assert alice.expect('411 NOTICE|1|1|2/3', timeout=3)
            assert alice.expect('411 NOTICE|1|1|3/3', timeout=3)
            assert alice.expect('420 SOLD|1|1|carol|120000|BID',timeout=2)
            assert alice.expect('400 ITEM_STARTED|1|3',timeout=2)
            assert bob.run('BUY_NOW') == '162 BOUGHT|3|50000'
            assert alice.expect('420 SOLD|1|3|bob|50000|BUY_NOW',timeout=2)
            assert bob.run('BID|15000',('2', '3', '5')) == '241 NO_ACTIVE_ITEM'
            assert bob.run('LIST_ITEMS').startswith('152 ITEMS|3|ACTIVE=0')
            assert alice.run('LEAVE_ROOM') == '142 LEFT_ROOM'
            assert alice.run('CREATE_ROOM|New room') == '140 ROOM_CREATED|3|New room'
            assert alice.run('ADD_ITEM|ResetTest|Monitor|100000|250000') == '150 ITEM_QUEUED|4'
            assert alice.run('START') == '160 AUCTION_STARTED'
            assert bob.run('LEAVE_ROOM') == '142 LEFT_ROOM'
            assert bob.run('JOIN_ROOM|3') == '141 JOINED_ROOM|3|New room'
            assert carol.run('LEAVE_ROOM') == '142 LEFT_ROOM'
            assert carol.run('JOIN_ROOM|3') == '141 JOINED_ROOM|3|New room'
            assert bob.run('BID|110000') == '161 BID_ACCEPTED|4|110000'
            assert alice.expect('411 NOTICE|3|4|1/3', timeout=3)
            assert carol.run('BID|120000') == '161 BID_ACCEPTED|4|120000'
            # The new bid cancels the old pending 2/3 and restarts from 1/3.
            assert alice.expect('411 NOTICE|3|4|1/3', timeout=3)
            assert alice.expect('411 NOTICE|3|4|2/3', timeout=3)
            assert alice.expect('411 NOTICE|3|4|3/3', timeout=3)
            assert alice.expect('420 SOLD|3|4|carol|120000|BID', timeout=2)
            assert alice.run('ADD_ITEM|RaceBuy|Phone|50000|100000') == '150 ITEM_QUEUED|5'
            assert alice.run('START') == '160 AUCTION_STARTED'
            outcomes = []
            def buy(peer):
                peer.send('BUY_NOW')
                while True:
                    reply = peer.get()
                    if reply.startswith('162 BOUGHT') or reply.startswith('241 NO_ACTIVE_ITEM'):
                        outcomes.append(reply)
                        return
            x = threading.Thread(target=buy, args=(bob,))
            y = threading.Thread(target=buy, args=(carol,))
            x.start(); y.start(); x.join(timeout=4); y.join(timeout=4)
            assert len(outcomes) == 2, outcomes
            assert sum(v.startswith('162 BOUGHT|5|100000') for v in outcomes) == 1, outcomes
            assert sum(v.startswith('241 NO_ACTIVE_ITEM') for v in outcomes) == 1, outcomes
            assert alice.expect('420 SOLD|3|5|', timeout=2)
            assert alice.run('LOGOUT') == '130 LOGGED_OUT'
            assert alice.run('LOGIN|alice|WrongPass',('2', '3', '5')) == '210 INVALID_CREDENTIALS'
            assert alice.run('LOGIN|alice|TestPass123') == '110 LOGGED_IN|alice'
            assert bob.run('LIST_ROOMS').startswith('143 ROOMS|3')
            assert bob.run('CHANGE_PASSWORD|BadOldPwd|AnotherPass123',('2', '3', '5')) == '216 INVALID_OLD_PASSWORD'
            assert bob.run('CHANGE_PASSWORD|TestPass123|AnotherPass123') == '112 PASSWORD_CHANGED'
            # Stream coalescing of multiple commands.
            bob.send_raw(b'WHOAMI\r\nWHOAMI\r\n')
            assert bob.expect('113 SESSION|bob|3') == '113 SESSION|bob|3'
            assert bob.expect('113 SESSION|bob|3') == '113 SESSION|bob|3'
            # oversized line: error and graceful disconnection
            intruder = Peer(port)
            intruder.send_raw(b'X'*2500+b'\r\n')
            assert intruder.expect('302 INVALID_OR_TOO_LONG_LINE').startswith('302 INVALID_OR_TOO_LONG_LINE')
            intruder.close()
            # 1 online account cannot log in again concurrently
            intruder = Peer(port)
            assert intruder.run('LOGIN|bob|AnotherPass123',('2', '3', '5')) == '214 USER_ALREADY_ONLINE'
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
            assert user.run('LOGIN|bob|TestPass123',('2', '3', '5')) == '210 INVALID_CREDENTIALS'
            assert user.run('LOGIN|bob|AnotherPass123') == '110 LOGGED_IN|bob'
            assert user.run('LIST_ROOMS') == '143 ROOMS|3' # rooms survive restart
            assert user.run('JOIN_ROOM|3') == '141 JOINED_ROOM|3|New room'
            assert user.run('LIST_ITEMS') == '152 ITEMS|2|ACTIVE=0'
            assert user.expect('153 ITEM|4|SOLD|ResetTest|Monitor|100000|250000|120000|carol|carol')
            user.close()
        finally:
            server.terminate(); server.communicate(timeout=2)
        print('PASS: PostgreSQL accounts, rooms, items survive restart; audit file contains no passwords')

if __name__ == '__main__': main()
