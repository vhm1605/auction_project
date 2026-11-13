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
        self.expect('OK|CONNECTED')

    def send(self, line):
        self.socket.sendall((line + '\n').encode('utf-8'))

    def send_raw(self, data):
        self.socket.sendall(data)

    def get(self, timeout=3):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            idx = self.pending.find(b'\n')
            if idx >= 0:
                msg = self.pending[:idx].decode('utf-8')
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

    def run(self, cmd, expected='OK|'):
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
            for peer,name in zip(peers,['alice','bob','carol','dave']):
                assert peer.run(f'REGISTER|{name}|TestPass123') == f'OK|REGISTERED|{name}'
                assert peer.run(f'LOGIN|{name}|TestPass123') == f'OK|LOGGED_IN|{name}'
            assert alice.run('CREATE_ROOM|Đấu giá thiết bị') == 'OK|ROOM_CREATED|1|Đấu giá thiết bị'
            assert alice.run('JOIN_ROOM|1','ERR|') == 'ERR|ALREADY_IN_A_ROOM'
            assert bob.run('JOIN_ROOM|1') == 'OK|JOINED_ROOM|1|Đấu giá thiết bị'
            assert carol.run('JOIN_ROOM|1').startswith('OK|JOINED_ROOM')
            assert dave.run('CREATE_ROOM|Other room').startswith('OK|ROOM_CREATED|2')
            assert bob.run('JOIN_ROOM|2','ERR|') == 'ERR|ALREADY_IN_A_ROOM'
            assert bob.run('ADD_ITEM|Bad|Test|100000|200000','ERR|') == 'ERR|ONLY_OWNER_CAN_SELL'
            assert bob.run('START','ERR|') == 'ERR|ONLY_OWNER_CAN_START'
            assert alice.run('ADD_ITEM|Bad|Test|9999999999999999999999|100000','ERR|').startswith('ERR|INVALID_PRICE')
            assert alice.run('ADD_ITEM|Bad|Test|100000|90000','ERR|').startswith('ERR|INVALID_PRICE')
            assert alice.run('ADD_ITEM|Laptop|Đã sử dụng|100000|200000').startswith('OK|ITEM_QUEUED|1')
            assert alice.run('ADD_ITEM|Phone|New|50000|90000').startswith('OK|ITEM_QUEUED|2')
            assert alice.run('WITHDRAW_ITEM|2') == 'OK|ITEM_WITHDRAWN|2'
            assert alice.run('ADD_ITEM|Mouse|USB|10000|50000').startswith('OK|ITEM_QUEUED|3')
            assert alice.run('START') == 'OK|AUCTION_STARTED'
            assert alice.run('WITHDRAW_ITEM|1','ERR|') == 'ERR|ONLY_QUEUED_ITEMS_CAN_BE_WITHDRAWN'
            assert bob.run('BID|105000','ERR|').startswith('ERR|BID_INVALID')
            assert bob.run('BID|9223372036854775808','ERR|').startswith('ERR|BID_INVALID')
            assert alice.run('BID|110000','ERR|') == 'ERR|SELLER_CANNOT_BID'
            bob.send_raw(b'BID|11')
            time.sleep(0.02)
            bob.send_raw(b'0000\n')
            assert bob.expect('OK|BID_ACCEPTED|1|110000') == 'OK|BID_ACCEPTED|1|110000'
            assert carol.run('BID|120000') == 'OK|BID_ACCEPTED|1|120000'
            # Wait through 3 notifications; winner must be most recent highest bidder.
            assert alice.expect('EVENT|NOTICE|1|1|1/3', timeout=3)
            assert alice.expect('EVENT|NOTICE|1|1|2/3', timeout=3)
            assert alice.expect('EVENT|NOTICE|1|1|3/3', timeout=3)
            assert alice.expect('EVENT|SOLD|1|1|carol|120000|BID',timeout=2)
            assert alice.expect('EVENT|ITEM_STARTED|1|3',timeout=2)
            assert bob.run('BUY_NOW') == 'OK|BOUGHT|3|50000'
            assert alice.expect('EVENT|SOLD|1|3|bob|50000|BUY_NOW',timeout=2)
            assert bob.run('BID|15000','ERR|') == 'ERR|NO_ACTIVE_ITEM'
            assert bob.run('LIST_ITEMS').startswith('OK|ITEMS|3|ACTIVE=0')
            assert alice.run('LEAVE_ROOM') == 'OK|LEFT_ROOM'
            assert alice.run('CREATE_ROOM|New room') == 'OK|ROOM_CREATED|3|New room'
            assert alice.run('ADD_ITEM|ResetTest|Monitor|100000|250000') == 'OK|ITEM_QUEUED|4'
            assert alice.run('START') == 'OK|AUCTION_STARTED'
            assert bob.run('LEAVE_ROOM') == 'OK|LEFT_ROOM'
            assert bob.run('JOIN_ROOM|3') == 'OK|JOINED_ROOM|3|New room'
            assert carol.run('LEAVE_ROOM') == 'OK|LEFT_ROOM'
            assert carol.run('JOIN_ROOM|3') == 'OK|JOINED_ROOM|3|New room'
            assert bob.run('BID|110000') == 'OK|BID_ACCEPTED|4|110000'
            assert alice.expect('EVENT|NOTICE|3|4|1/3', timeout=3)
            assert carol.run('BID|120000') == 'OK|BID_ACCEPTED|4|120000'
            # The new bid cancels the old pending 2/3 and restarts from 1/3.
            assert alice.expect('EVENT|NOTICE|3|4|1/3', timeout=3)
            assert alice.expect('EVENT|NOTICE|3|4|2/3', timeout=3)
            assert alice.expect('EVENT|NOTICE|3|4|3/3', timeout=3)
            assert alice.expect('EVENT|SOLD|3|4|carol|120000|BID', timeout=2)
            assert alice.run('ADD_ITEM|RaceBuy|Phone|50000|100000') == 'OK|ITEM_QUEUED|5'
            assert alice.run('START') == 'OK|AUCTION_STARTED'
            outcomes = []
            def buy(peer):
                peer.send('BUY_NOW')
                while True:
                    reply = peer.get()
                    if reply.startswith('OK|BOUGHT') or reply.startswith('ERR|NO_ACTIVE_ITEM'):
                        outcomes.append(reply)
                        return
            x = threading.Thread(target=buy, args=(bob,))
            y = threading.Thread(target=buy, args=(carol,))
            x.start(); y.start(); x.join(timeout=4); y.join(timeout=4)
            assert len(outcomes) == 2, outcomes
            assert sum(v.startswith('OK|BOUGHT|5|100000') for v in outcomes) == 1, outcomes
            assert sum(v.startswith('ERR|NO_ACTIVE_ITEM') for v in outcomes) == 1, outcomes
            assert alice.expect('EVENT|SOLD|3|5|', timeout=2)
            assert alice.run('LOGOUT') == 'OK|LOGGED_OUT'
            assert alice.run('LOGIN|alice|WrongPass','ERR|') == 'ERR|INVALID_CREDENTIALS'
            assert alice.run('LOGIN|alice|TestPass123') == 'OK|LOGGED_IN|alice'
            assert bob.run('LIST_ROOMS').startswith('OK|ROOMS|3')
            assert bob.run('CHANGE_PASSWORD|BadOldPwd|AnotherPass123','ERR|') == 'ERR|INVALID_OLD_PASSWORD'
            assert bob.run('CHANGE_PASSWORD|TestPass123|AnotherPass123') == 'OK|PASSWORD_CHANGED'
            # Stream coalescing of multiple commands.
            bob.send_raw(b'WHOAMI\nWHOAMI\n')
            assert bob.expect('OK|SESSION|bob|3') == 'OK|SESSION|bob|3'
            assert bob.expect('OK|SESSION|bob|3') == 'OK|SESSION|bob|3'
            # oversized line: error and graceful disconnection
            intruder = Peer(port)
            intruder.send_raw(b'X'*2500+b'\n')
            assert intruder.expect('ERR|INVALID_OR_TOO_LONG_LINE').startswith('ERR|INVALID_OR_TOO_LONG_LINE')
            intruder.close()
            # 1 online account cannot log in again concurrently
            intruder = Peer(port)
            assert intruder.run('LOGIN|bob|AnotherPass123','ERR|') == 'ERR|USER_ALREADY_ONLINE'
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
            assert user.run('LOGIN|bob|TestPass123','ERR|') == 'ERR|INVALID_CREDENTIALS'
            assert user.run('LOGIN|bob|AnotherPass123') == 'OK|LOGGED_IN|bob'
            assert user.run('LIST_ROOMS') == 'OK|ROOMS|3' # rooms survive restart
            assert user.run('JOIN_ROOM|3') == 'OK|JOINED_ROOM|3|New room'
            assert user.run('LIST_ITEMS') == 'OK|ITEMS|2|ACTIVE=0'
            assert user.expect('ITEM|4|SOLD|ResetTest|Monitor|100000|250000|120000|carol|carol')
            user.close()
        finally:
            server.terminate(); server.communicate(timeout=2)
        print('PASS: PostgreSQL accounts, rooms, items survive restart; audit file contains no passwords')

if __name__ == '__main__': main()
