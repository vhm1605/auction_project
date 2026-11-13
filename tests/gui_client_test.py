#!/usr/bin/env python3
"""Headless UI integration: run under xvfb-run on Linux."""
import importlib.util
import socket
import subprocess
import tempfile
import time
import tkinter as tk
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'tools' / 'gui_client.py'
spec = importlib.util.spec_from_file_location('auction_gui_client', SRC)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]



def expect(root, condition, message, seconds=4):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        root.update()
        if condition():
            return
        time.sleep(0.015)
    raise AssertionError(message)


def read_log(app):
    return app.log.get('1.0', 'end')


def main():
    assert mod.check_field('Thiết bị', 'Tên', 80) == 'Thiết bị'
    for bad in ('bad|pipe', '', 'foo\nbar', 'x' * 90):
        try:
            mod.check_field(bad, 'Tên', 80)
        except ValueError:
            pass
        else:
            raise AssertionError('client accepted invalid field')
    assert mod.check_number('110000', 'giá') == '110000'
    # Test GUI's network framing with TCP-like fragmented/coalesced incoming bytes.
    rx, tx = socket.socketpair()
    rx.setblocking(False)
    transport = mod.LineTransport()
    transport.sock = rx
    try:
        tx.sendall(b'OK|A\nEVENT|NOT')
        assert transport.poll() == ['OK|A']
        tx.sendall(b'ICE\nOK|B\n')
        assert transport.poll() == ['EVENT|NOTICE', 'OK|B']
    finally:
        transport.close()
        tx.close()
    for bad in ('9000000000000000000', '-1', '20a', '0'):
        try:
            mod.check_number(bad, 'giá')
        except ValueError:
            pass
        else:
            raise AssertionError('client accepted invalid number')

    port = free_port()
    with tempfile.TemporaryDirectory(prefix='auction-gui-test-') as directory:
        server = subprocess.Popen([str(ROOT / 'build' / 'auction_server'), '--port', str(port),
                                   '--data-dir', directory, '--bid-timeout', '1', '--notice-gap', '1'],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        root = None
        bob = None
        try:
            for _ in range(80):
                try:
                    with socket.create_connection(('127.0.0.1', port), timeout=0.1):
                        break
                except OSError:
                    time.sleep(0.025)
            else:
                raise AssertionError('server did not start')
            root = tk.Tk()
            root.withdraw()  # Headless display still creates all widgets.
            app = mod.AuctionApp(root, '127.0.0.1', port)
            expect(root, lambda: 'CONNECTED' in read_log(app), 'GUI failed to connect')
            app.user_entry.insert(0, 'alice')
            app.pass_entry.insert(0, 'TestPass123')
            app.register()
            expect(root, lambda: 'REGISTERED' in read_log(app), 'register failed')
            app.pass_entry.insert(0, 'TestPass123')
            app.login()
            expect(root, lambda: app.username == 'alice', 'GUI failed login')
            app.room_name.insert(0, 'Phòng đấu giá')
            app.create_room()
            expect(root, lambda: app.room_id == 1, 'GUI create_room failed')
            app.item_name.insert(0, 'Laptop')
            app.item_desc.insert(0, 'Đã sử dụng')
            app.item_start.insert(0, '100000')
            app.item_buy.insert(0, '200000')
            app.add_item()
            expect(root, lambda: '1' in app.items, 'GUI item list not populated')
            assert app.items['1'][0] == 'QUEUED'
            app.send('START')
            expect(root, lambda: app.active_id == 1, 'GUI failed display active item')
            bob = mod.LineTransport()
            bob.connect('127.0.0.1', port)
            def bob_cmd(cmd, wanted):
                bob.send_line(cmd)
                until = time.monotonic() + 4
                while time.monotonic() < until:
                    for line in bob.poll():
                        if line.startswith(wanted):
                            return line
                    root.update()
                    time.sleep(0.01)
                raise AssertionError(f'failed {cmd} expected {wanted}')
            assert bob_cmd('REGISTER|bob|TestPass123','OK|REGISTERED')
            assert bob_cmd('LOGIN|bob|TestPass123','OK|LOGGED_IN')
            assert bob_cmd('JOIN_ROOM|1','OK|JOINED_ROOM')
            assert bob_cmd('BID|110000','OK|BID_ACCEPTED')
            expect(root, lambda: '110.000 đ' == app.current_label.get(), 'GUI did not show live bid')
            expect(root, lambda: 'ĐÃ BÁN #1 cho bob' in read_log(app), 'GUI did not show sold event', seconds=5)
            expect(root, lambda: app.active_id == 0, 'GUI did not refresh sold state')
            print('PASS: GUI Tkinter render + server connect, register/login, room, item, bid and live sold events')
        finally:
            if bob:
                bob.close()
            if root:
                root.destroy()
            server.terminate()
            try:
                server.wait(timeout=4)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


if __name__ == '__main__':
    main()
