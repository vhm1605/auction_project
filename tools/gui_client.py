#!/usr/bin/env python3
"""Tkinter desktop GUI for the existing C++ line-based TCP auction server.

UI-only optional module: all server-side socket, authentication, room authorization,
auction timer and price rules stay in the C++ program. No third-party Python packages.
"""
import argparse
import select
import socket
import tkinter as tk
from tkinter import messagebox, ttk

MAX_LINE = 2048
MAX_SEND_QUEUE = 65536
BG = "#eef2f7"
INK = "#172338"
MUTED = "#53657a"
NAVY = "#17395f"
TEAL = "#047f80"
BORDER = "#d7e0e9"


class LineTransport:
    """Nonblocking TCP with newline framing; no assumption about recv boundaries."""

    def __init__(self):
        self.sock = None
        self.incoming = bytearray()
        self.outgoing = bytearray()

    @property
    def connected(self):
        return self.sock is not None

    def connect(self, host, port):
        self.close()
        sock = socket.create_connection((host, port), timeout=3)
        sock.setblocking(False)
        self.sock = sock

    def close(self):
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = None
        self.incoming.clear()
        self.outgoing.clear()

    def send_line(self, command):
        if not self.connected:
            raise ConnectionError("Chưa kết nối server")
        if not isinstance(command, str) or any(c in command for c in "\n\r\x00"):
            raise ValueError("Lệnh chứa ký tự xuống dòng hoặc NUL")
        data = command.encode("utf-8", errors="strict")
        if len(data) > MAX_LINE:
            raise ValueError("Lệnh quá 2048 byte")
        if len(self.outgoing) + len(data) + 1 > MAX_SEND_QUEUE:
            raise ValueError("Hàng đợi gửi vượt 64 KiB")
        self.outgoing.extend(data + b"\n")

    def poll(self):
        if self.sock is None:
            return []
        readable, writable, exceptional = select.select(
            [self.sock], [self.sock] if self.outgoing else [], [self.sock], 0)
        if exceptional:
            raise ConnectionError("Lỗi socket")
        if writable:
            try:
                sent = self.sock.send(self.outgoing)
            except (BlockingIOError, InterruptedError):
                sent = None
            if sent == 0 and self.outgoing:
                raise ConnectionError("Không gửi được dữ liệu tới server")
            if sent is not None:
                del self.outgoing[:sent]
        lines = []
        if readable:
            # Read once per UI cycle, so a busy server cannot freeze the interface.
            try:
                chunk = self.sock.recv(4096)
            except (BlockingIOError, InterruptedError):
                chunk = None
            if chunk == b"":
                raise ConnectionError("Server đã ngắt kết nối")
            if chunk:
                self.incoming.extend(chunk)
                if len(self.incoming) > MAX_LINE + 4096:
                    raise ConnectionError("Server gửi dữ liệu vượt giới hạn")
                while b"\n" in self.incoming:
                    raw, _, rem = self.incoming.partition(b"\n")
                    self.incoming = bytearray(rem)
                    if raw.endswith(b"\r"):
                        raw = raw[:-1]
                    if len(raw) > MAX_LINE:
                        raise ConnectionError("Dòng trả về vượt 2048 byte")
                    lines.append(raw.decode("utf-8", errors="strict"))
                if len(self.incoming) > MAX_LINE:
                    raise ConnectionError("Dòng chưa hoàn tất vượt 2048 byte")
        return lines


def check_field(value, label, max_bytes=240):
    if not value or value.isspace():
        raise ValueError(f"{label} không được để trống")
    if any(ord(c) < 32 or ord(c) == 127 or c == "|" for c in value):
        raise ValueError(f"{label} không được chứa dấu | hoặc ký tự điều khiển")
    if len(value.encode("utf-8")) > max_bytes:
        raise ValueError(f"{label} vượt {max_bytes} byte UTF-8")
    return value


def check_number(value, label):
    if not value or not value.isascii() or not value.isdecimal() or len(value) > 16:
        raise ValueError(f"{label} phải là số nguyên dương hợp lệ")
    number = int(value)
    if not (1 <= number <= 9_000_000_000_000_000):
        raise ValueError(f"{label} vượt giới hạn")
    return str(number)


def price(value):
    try:
        return f"{int(value):,} đ".replace(",", ".")
    except (ValueError, TypeError):
        return str(value)


class AuctionApp:
    def __init__(self, root, host, port):
        self.root = root
        self.root.title("AUCTION | Đấu giá trực tuyến")
        self.root.geometry("1200x790")
        self.root.minsize(1000, 700)
        self.root.configure(bg=BG)
        self.tcp = LineTransport()
        self.username = ""
        self.room_id = 0
        self.room_owner = ""
        self.active_id = 0
        self.active_price = 0
        self.buy_price = 0
        self.rooms = {}
        self.items = {}
        self.pending_rooms = False
        self.pending_items = False
        self.note = tk.StringVar(value="Chưa kết nối")
        self.account = tk.StringVar(value="Chưa đăng nhập")
        self.room_label = tk.StringVar(value="Chưa tham gia phòng")
        self.active_label = tk.StringVar(value="Chưa có phiên đang mở")
        self.current_label = tk.StringVar(value="—")
        self.buy_label = tk.StringVar(value="—")
        self.host = tk.StringVar(value=host)
        self.port = tk.StringVar(value=str(port))
        self._style()
        self._build()
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.after(90, self.poll)
        # Initial connection should happen only after UI has fully appeared.
        self.root.after(250, self.connect)

    def _style(self):
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure("TFrame", background=BG)
        style.configure("Pane.TFrame", background="white")
        style.configure("TLabel", background=BG, foreground=INK, font=("Arial", 10))
        style.configure("Pane.TLabel", background="white", foreground=INK, font=("Arial", 10))
        style.configure("Head.TLabel", background=BG, foreground=NAVY, font=("Arial", 20, "bold"))
        style.configure("Section.TLabel", background="white", foreground=NAVY, font=("Arial", 12, "bold"))
        style.configure("Stat.TLabel", background="white", foreground=NAVY, font=("Arial", 11, "bold"))
        style.configure("Hint.TLabel", background="white", foreground=MUTED, font=("Arial", 9))
        style.configure("TButton", font=("Arial", 10), padding=(12, 7))
        style.configure("Accent.TButton", background=TEAL, foreground="white", font=("Arial", 10, "bold"))
        style.map("Accent.TButton", background=[("active", "#06666b"), ("disabled", "#b7c5cc")])
        style.configure("TNotebook", background=BG, borderwidth=0)
        style.configure("TNotebook.Tab", font=("Arial", 10, "bold"), padding=(15, 10))
        style.configure("Treeview", rowheight=29, font=("Arial", 10), background="white", fieldbackground="white")
        style.configure("Treeview.Heading", background="#e9f1f5", foreground=NAVY, font=("Arial", 10, "bold"))

    def _build(self):
        top = ttk.Frame(self.root)
        top.pack(fill="x", padx=22, pady=(14, 10))
        ttk.Label(top, text="AUCTION", style="Head.TLabel").pack(side="left")
        ttk.Label(top, text="  •  HỆ THỐNG ĐẤU GIÁ TCP", foreground=MUTED).pack(side="left", padx=12)
        ttk.Label(top, textvariable=self.note, foreground=TEAL).pack(side="right")

        conn = ttk.Frame(self.root)
        conn.pack(fill="x", padx=22, pady=(0, 12))
        ttk.Label(conn, text="Server IP").pack(side="left")
        ttk.Entry(conn, textvariable=self.host, width=17).pack(side="left", padx=(8, 14))
        ttk.Label(conn, text="Port").pack(side="left")
        ttk.Entry(conn, textvariable=self.port, width=7).pack(side="left", padx=(8, 12))
        ttk.Button(conn, text="Kết nối", command=self.connect).pack(side="left", padx=3)
        ttk.Button(conn, text="Ngắt kết nối", command=self.disconnect).pack(side="left", padx=3)
        ttk.Label(conn, textvariable=self.account).pack(side="right", padx=5)

        self.tabs = ttk.Notebook(self.root)
        self.tabs.pack(fill="both", expand=True, padx=22, pady=(0, 12))
        self.tab_auth = ttk.Frame(self.tabs, padding=15)
        self.tab_rooms = ttk.Frame(self.tabs, padding=15)
        self.tab_auction = ttk.Frame(self.tabs, padding=15)
        self.tabs.add(self.tab_auth, text="Tài khoản")
        self.tabs.add(self.tab_rooms, text="Phòng đấu giá")
        self.tabs.add(self.tab_auction, text="Phiên đấu giá")
        self._build_auth()
        self._build_rooms()
        self._build_auction()

        footer = ttk.Frame(self.root)
        footer.pack(fill="both", padx=22, pady=(0, 15))
        ttk.Label(footer, text="THÔNG BÁO TRỰC TIẾP", foreground=NAVY, font=("Arial", 11, "bold")).pack(anchor="w", pady=(0, 6))
        lf = ttk.Frame(footer)
        lf.pack(fill="both")
        self.log = tk.Text(lf, height=5, wrap="word", bg="#12283f", fg="#e8f5fa",
                           insertbackground="white", relief="flat", font=("Consolas", 10))
        self.log.pack(side="left", fill="both", expand=True)
        sb = ttk.Scrollbar(lf, orient="vertical", command=self.log.yview)
        sb.pack(side="right", fill="y")
        self.log.configure(yscrollcommand=sb.set, state="disabled")
        self.log.tag_configure("err", foreground="#ffb2ac")
        self.log.tag_configure("event", foreground="#88e3cf")
        self.log.tag_configure("ok", foreground="#d4e6fd")
        # Reserve bottom status/event area first; the notebook uses remaining height.
        self.tabs.pack_forget()
        footer.pack_forget()
        footer.pack(side="bottom", fill="x", padx=22, pady=(0, 12))
        self.tabs.pack(side="top", fill="both", expand=True, padx=22, pady=(0, 8))

    def _panel(self, parent, title):
        f = ttk.Frame(parent, style="Pane.TFrame", padding=18)
        ttk.Label(f, text=title, style="Section.TLabel").pack(anchor="w", pady=(0, 14))
        return f

    @staticmethod
    def _entry(parent, label, secret=False, width=28):
        row = ttk.Frame(parent, style="Pane.TFrame")
        row.pack(fill="x", pady=4)
        ttk.Label(row, text=label, style="Pane.TLabel", width=17).pack(side="left")
        entry = ttk.Entry(row, width=width, show="*" if secret else "")
        entry.pack(side="left", fill="x", expand=True)
        return entry

    def _build_auth(self):
        row = ttk.Frame(self.tab_auth)
        row.pack(fill="both", expand=True)
        left = self._panel(row, "Đăng nhập / đăng ký")
        left.pack(side="left", fill="both", expand=True, padx=(0, 8))
        self.user_entry = self._entry(left, "Tên tài khoản")
        self.pass_entry = self._entry(left, "Mật khẩu", secret=True)
        btn = ttk.Frame(left, style="Pane.TFrame")
        btn.pack(anchor="w", pady=14)
        ttk.Button(btn, text="Đăng nhập", style="Accent.TButton", command=self.login).pack(side="left", padx=(0, 8))
        ttk.Button(btn, text="Đăng ký", command=self.register).pack(side="left", padx=(0, 8))
        ttk.Button(btn, text="Đăng xuất", command=lambda: self.send("LOGOUT")).pack(side="left")
        ttk.Label(left, text="Tên: 3–24 chữ/số/_  •  Mật khẩu: 8–72 ký tự ASCII", style="Hint.TLabel").pack(anchor="w")

        right = self._panel(row, "Bảo mật tài khoản")
        right.pack(side="left", fill="both", expand=True, padx=(8, 0))
        self.old_pass = self._entry(right, "Mật khẩu hiện tại", secret=True)
        self.new_pass = self._entry(right, "Mật khẩu mới", secret=True)
        ttk.Button(right, text="Đổi mật khẩu", command=self.change_password).pack(anchor="w", pady=14)
        ttk.Label(right, text="Phần server C++ xác thực toàn bộ thao tác.\nClient không được tự quyết định quyền truy cập.",
                  style="Hint.TLabel", justify="left").pack(anchor="w")

    def _build_rooms(self):
        head = ttk.Frame(self.tab_rooms)
        head.pack(fill="x", pady=(0, 12))
        ttk.Label(head, textvariable=self.room_label, font=("Arial", 12, "bold"), foreground=NAVY).pack(side="left")
        ttk.Button(head, text="Làm mới danh sách", command=lambda: self.send("LIST_ROOMS")).pack(side="right")

        panel = self._panel(self.tab_rooms, "Danh sách phòng đang hoạt động")
        panel.pack(fill="both", expand=True)
        self.rooms_tree = ttk.Treeview(panel, columns=("id", "name", "owner", "members"), show="headings", height=9)
        for key, title, width in [("id", "ID", 55), ("name", "Tên phòng", 310), ("owner", "Chủ phòng", 140), ("members", "Số người", 80)]:
            self.rooms_tree.heading(key, text=title)
            self.rooms_tree.column(key, width=width, anchor="w")
        self.rooms_tree.pack(fill="both", expand=True)
        self.rooms_tree.bind("<Double-1>", lambda _e: self.join_room())
        actions = ttk.Frame(panel, style="Pane.TFrame")
        actions.pack(fill="x", pady=(12, 0))
        ttk.Button(actions, text="Vào phòng đã chọn", style="Accent.TButton", command=self.join_room).pack(side="left", padx=(0, 10))
        ttk.Button(actions, text="Rời phòng", command=lambda: self.send("LEAVE_ROOM")).pack(side="left")
        create = self._panel(self.tab_rooms, "Tạo phòng mới")
        create.pack(fill="x", pady=(12, 0))
        cr = ttk.Frame(create, style="Pane.TFrame")
        cr.pack(fill="x")
        self.room_name = ttk.Entry(cr)
        self.room_name.pack(side="left", fill="x", expand=True, padx=(0, 8))
        ttk.Button(cr, text="+ Tạo phòng", style="Accent.TButton", command=self.create_room).pack(side="left")

    def _build_auction(self):
        # Scrollbar keeps every auction action accessible on small laptop screens.
        canvas = tk.Canvas(self.tab_auction, borderwidth=0, highlightthickness=0, bg=BG)
        scrollbar = ttk.Scrollbar(self.tab_auction, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side="right", fill="y")
        canvas.pack(side="left", fill="both", expand=True)
        inner = ttk.Frame(canvas)
        window = canvas.create_window((0, 0), window=inner, anchor="nw")
        inner.bind("<Configure>", lambda _e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.bind("<Configure>", lambda e: canvas.itemconfigure(window, width=e.width))
        stats = ttk.Frame(inner)
        stats.pack(fill="x", pady=(0, 12))
        for name, var in [("VẬT PHẨM HIỆN TẠI", self.active_label),
                          ("GIÁ HIỆN TẠI", self.current_label),
                          ("GIÁ MUA NGAY", self.buy_label)]:
            frame = ttk.Frame(stats, style="Pane.TFrame", padding=12)
            frame.pack(side="left", fill="x", expand=True, padx=(0, 8))
            ttk.Label(frame, text=name, style="Hint.TLabel").pack(anchor="w")
            ttk.Label(frame, textvariable=var, style="Stat.TLabel").pack(anchor="w", pady=(5, 0))

        content = ttk.Frame(inner)
        content.pack(fill="both", expand=True)
        # Two-column design keeps all bid and create-item buttons visible at 790px height.
        content.columnconfigure(0, weight=7)
        content.columnconfigure(1, weight=4)
        content.rowconfigure(0, weight=1)

        item_panel = self._panel(content, "Các vật phẩm trong phòng")
        item_panel.grid(row=0, column=0, sticky="nsew", padx=(0, 8))
        self.items_tree = ttk.Treeview(item_panel, columns=("id", "status", "name", "start", "current", "buy", "leader"), show="headings", height=7)
        for key, title, width in [("id", "ID", 42), ("status", "Trạng thái", 86), ("name", "Vật phẩm", 162),
                                  ("start", "Khởi điểm", 111), ("current", "Giá hiện tại", 118),
                                  ("buy", "Mua ngay", 115), ("leader", "Người dẫn", 95)]:
            self.items_tree.heading(key, text=title)
            self.items_tree.column(key, width=width, minwidth=35, anchor="w", stretch=True)
        self.items_tree.pack(fill="both", expand=True)
        cmds = ttk.Frame(item_panel, style="Pane.TFrame")
        cmds.pack(fill="x", pady=(10, 0))
        ttk.Button(cmds, text="Làm mới", command=lambda: self.send("LIST_ITEMS")).pack(side="left", padx=(0, 8))
        ttk.Button(cmds, text="Bắt đầu phiên", command=lambda: self.send("START")).pack(side="left", padx=(0, 8))
        ttk.Button(cmds, text="Rút vật phẩm", command=self.withdraw).pack(side="left")

        right = ttk.Frame(content)
        right.grid(row=0, column=1, sticky="nsew")
        buy = self._panel(right, "Tham gia đấu giá")
        buy.pack(fill="x", pady=(0, 10))
        self.bid_entry = self._entry(buy, "Giá muốn tố", width=19)
        actions = ttk.Frame(buy, style="Pane.TFrame")
        actions.pack(anchor="w", pady=(8, 0))
        ttk.Button(actions, text="Đặt giá", style="Accent.TButton", command=self.bid).pack(side="left", padx=(0, 8))
        ttk.Button(actions, text="Mua ngay", command=self.buy_now).pack(side="left")
        ttk.Label(buy, text="Mỗi bước tố ít nhất 10.000đ", style="Hint.TLabel").pack(anchor="w", pady=(10, 0))

        sell = self._panel(right, "Đăng vật phẩm (chủ phòng)")
        sell.pack(fill="x")
        self.item_name = self._entry(sell, "Tên vật phẩm", width=19)
        self.item_desc = self._entry(sell, "Mô tả", width=19)
        self.item_start = self._entry(sell, "Giá khởi điểm", width=19)
        self.item_buy = self._entry(sell, "Giá mua ngay", width=19)
        ttk.Button(sell, text="+ Đăng vật phẩm", style="Accent.TButton", command=self.add_item).pack(anchor="w", pady=(9, 0))

    def _log(self, content, tag="ok"):
        self.log.configure(state="normal")
        self.log.insert("end", content + "\n", tag)
        self.log.see("end")
        self.log.configure(state="disabled")

    def connect(self):
        try:
            host = self.host.get().strip()
            port_text = self.port.get().strip()
            if not host or not port_text.isascii() or not port_text.isdecimal() or not (1 <= int(port_text) <= 65535):
                raise ValueError("IP hoặc cổng không hợp lệ")
            self._reset_session()
            self.tcp.connect(host, int(port_text))
            self.note.set(f"Đã kết nối: {host}:{port_text}")
            self._log(f"Đã kết nối tới {host}:{port_text}")
        except (OSError, ValueError, ConnectionError) as e:
            self.note.set("Không kết nối được")
            self._log(f"LỖI KẾT NỐI: {e}", "err")

    def disconnect(self):
        self.tcp.close()
        self._reset_session()
        self.note.set("Chưa kết nối")
        self._log("Đã ngắt kết nối")

    def _reset_session(self):
        self.username = ""
        self.room_id = 0
        self.room_owner = ""
        self.active_id = 0
        self.rooms.clear()
        self.items.clear()
        self.account.set("Chưa đăng nhập")
        self.room_label.set("Chưa tham gia phòng")
        self.active_label.set("Chưa có phiên đang mở")
        self.current_label.set("—")
        self.buy_label.set("—")
        if hasattr(self, "rooms_tree"):
            self.rooms_tree.delete(*self.rooms_tree.get_children())
            self.items_tree.delete(*self.items_tree.get_children())

    def send(self, command):
        try:
            self.tcp.send_line(command)
        except (ValueError, OSError, ConnectionError) as e:
            self._log(f"Không gửi được: {e}", "err")

    def login(self):
        self._auth_action("LOGIN")

    def register(self):
        self._auth_action("REGISTER")

    def _auth_action(self, action):
        try:
            user = self.user_entry.get()
            password = self.pass_entry.get()
            if not (3 <= len(user) <= 24) or not all(c.isascii() and (c.isalnum() or c == "_") for c in user):
                raise ValueError("Tên tài khoản phải dài 3–24 ký tự chữ cái, số hoặc _")
            if not (8 <= len(password) <= 72) or not all(33 <= ord(c) <= 126 and c != "|" for c in password):
                raise ValueError("Mật khẩu phải dài 8–72 ký tự ASCII không có khoảng trắng hoặc dấu |")
            self.send(f"{action}|{user}|{password}")
            self.pass_entry.delete(0, "end")
        except ValueError as e:
            messagebox.showwarning("Dữ liệu không hợp lệ", str(e))

    def change_password(self):
        old, new = self.old_pass.get(), self.new_pass.get()
        if any(not (8 <= len(p) <= 72 and all(33 <= ord(c) <= 126 and c != "|" for c in p)) for p in (old, new)):
            messagebox.showwarning("Dữ liệu không hợp lệ", "Mật khẩu phải có 8–72 ký tự ASCII (không dấu |, không khoảng trắng)")
            return
        self.send(f"CHANGE_PASSWORD|{old}|{new}")
        self.old_pass.delete(0, "end")
        self.new_pass.delete(0, "end")

    def create_room(self):
        try:
            name = check_field(self.room_name.get().strip(), "Tên phòng", 64)
            self.send("CREATE_ROOM|" + name)
        except ValueError as e:
            messagebox.showwarning("Dữ liệu không hợp lệ", str(e))

    def join_room(self):
        choice = self.rooms_tree.selection()
        if not choice:
            messagebox.showinfo("Chọn phòng", "Hãy chọn một phòng trong danh sách")
            return
        self.send("JOIN_ROOM|" + choice[0])

    def add_item(self):
        try:
            name = check_field(self.item_name.get().strip(), "Tên vật phẩm", 80)
            description = check_field(self.item_desc.get().strip(), "Mô tả", 240)
            start = check_number(self.item_start.get().strip(), "Giá khởi điểm")
            buy = check_number(self.item_buy.get().strip(), "Giá mua ngay")
            if int(buy) < int(start) + 10000:
                raise ValueError("Giá mua ngay phải cao hơn giá khởi điểm ít nhất 10.000đ")
            self.send("|".join(("ADD_ITEM", name, description, start, buy)))
        except ValueError as e:
            messagebox.showwarning("Dữ liệu không hợp lệ", str(e))

    def withdraw(self):
        choice = self.items_tree.selection()
        if not choice:
            messagebox.showinfo("Chọn vật phẩm", "Hãy chọn vật phẩm QUEUED cần rút")
            return
        self.send("WITHDRAW_ITEM|" + choice[0])

    def bid(self):
        try:
            amount = check_number(self.bid_entry.get().strip(), "Giá tố")
            self.send("BID|" + amount)
        except ValueError as e:
            messagebox.showwarning("Dữ liệu không hợp lệ", str(e))

    def buy_now(self):
        if self.active_id == 0:
            messagebox.showwarning("Chưa có vật phẩm", "Hiện chưa có vật phẩm đang bán")
            return
        if messagebox.askyesno("Xác nhận mua ngay", f"Mua ngay vật phẩm #{self.active_id} với giá {self.buy_label.get()}?"):
            self.send("BUY_NOW")

    def poll(self):
        try:
            for line in self.tcp.poll():
                self._process_line(line)
        except (OSError, ConnectionError, UnicodeError, ValueError) as e:
            self._log(f"Mất kết nối: {e}", "err")
            self.disconnect()
        finally:
            self.root.after(90, self.poll)

    def _process_line(self, line):
        fields = line.split("|")
        kind = fields[0]
        if kind == "ROOM" and len(fields) >= 5:
            rid = fields[1]
            if not rid.isdecimal():
                return
            self.rooms[rid] = fields[2:5]
            self.rooms_tree.insert("", "end", iid=rid, values=(rid, fields[2], fields[3], fields[4]))
            return
        if kind == "ITEM" and len(fields) >= 10:
            iid = fields[1]
            if not iid.isdecimal():
                return
            self.items[iid] = fields[2:10]
            self.items_tree.insert("", "end", iid=iid,
                                   values=(iid, fields[2], fields[3], price(fields[5]),
                                           price(fields[7]), price(fields[6]), fields[8]))
            if fields[2] == "ACTIVE":
                self.active_id = int(iid)
                self.active_label.set(f"#{iid} · {fields[3][:24]}")
                self.current_label.set(price(fields[7]))
                self.buy_label.set(price(fields[6]))
            return
        if kind == "ERR":
            self._log("TỪ CHỐI: " + " | ".join(fields[1:]), "err")
            return
        if kind == "EVENT":
            self._handle_event(fields[1:])
            return
        if kind == "OK":
            self._handle_ok(fields[1:])
        self._log("SERVER: " + " | ".join(fields), "ok")

    def _handle_ok(self, f):
        if not f:
            return
        action = f[0]
        if action == "LOGGED_IN" and len(f) >= 2:
            self.username = f[1]
            self.account.set("Đã đăng nhập: " + self.username)
            self.send("LIST_ROOMS")
            self.tabs.select(self.tab_rooms)
        elif action == "LOGGED_OUT":
            self._reset_session()
        elif action in ("ROOM_CREATED", "JOINED_ROOM") and len(f) >= 3:
            self.room_id = int(f[1]) if f[1].isdecimal() else 0
            self.room_label.set(f"Đang ở phòng #{self.room_id}: {f[2]}")
            self.room_owner = self.username if action == "ROOM_CREATED" else ""
            self.send("LIST_ITEMS")
            self.send("LIST_ROOMS")
            self.tabs.select(self.tab_auction)
        elif action == "LEFT_ROOM":
            self.room_id = 0
            self.room_label.set("Chưa tham gia phòng")
            self._clear_items()
            self.send("LIST_ROOMS")
            self.tabs.select(self.tab_rooms)
        elif action == "ROOMS":
            self.rooms.clear()
            self.rooms_tree.delete(*self.rooms_tree.get_children())
        elif action == "ITEMS":
            self._clear_items()
        elif action == "SESSION" and len(f) >= 3:
            self.username = f[1]
            self.room_id = int(f[2]) if f[2].isdecimal() else 0
        elif action in ("ITEM_QUEUED", "ITEM_WITHDRAWN", "AUCTION_STARTED", "BID_ACCEPTED", "BOUGHT"):
            self.send("LIST_ITEMS")
        elif action == "REGISTERED":
            self._log("Đăng ký thành công. Hãy bấm Đăng nhập.", "event")

    def _clear_items(self):
        self.items.clear()
        self.active_id = 0
        self.items_tree.delete(*self.items_tree.get_children())
        self.active_label.set("Chưa có phiên đang mở")
        self.current_label.set("—")
        self.buy_label.set("—")

    def _handle_event(self, f):
        if not f:
            return
        event = f[0]
        if event == "ITEM_STARTED" and len(f) >= 7:
            self._log(f"BẮT ĐẦU: #{f[2]} · {f[3]} — Giá khởi điểm {price(f[5])}", "event")
            self.send("LIST_ITEMS")
        elif event == "NEW_BID" and len(f) >= 5:
            self._log(f"GIÁ MỚI: {f[3]} trả {price(f[4])} cho vật phẩm #{f[2]}", "event")
            self.current_label.set(price(f[4]))
            self.send("LIST_ITEMS")
        elif event == "NOTICE" and len(f) >= 6:
            self._log(f"THÔNG BÁO {f[3]}: {f[4]} đang dẫn {price(f[5])}", "event")
        elif event == "SOLD" and len(f) >= 6:
            self._log(f"ĐÃ BÁN #{f[2]} cho {f[3]} · {price(f[4])} ({f[5]})", "event")
            self.send("LIST_ITEMS")
        elif event == "ITEM_QUEUED" and len(f) >= 4:
            self._log(f"Vật phẩm #{f[2]} {f[3]} đã được thêm vào hàng đợi", "event")
            self.send("LIST_ITEMS")
        elif event == "ITEM_WITHDRAWN" and len(f) >= 3:
            self._log(f"Vật phẩm #{f[2]} đã bị rút", "event")
            self.send("LIST_ITEMS")
        elif event == "QUEUE_EMPTY":
            self._log("Hàng đợi vật phẩm đã hết", "event")
            self.send("LIST_ITEMS")
        elif event in ("MEMBER_JOINED", "MEMBER_LEFT") and len(f) >= 3:
            self._log(f"Thành viên {f[2]} {'vào' if event == 'MEMBER_JOINED' else 'rời'} phòng", "event")
            self.send("LIST_ROOMS")
        else:
            self._log("THÔNG BÁO: " + " | ".join(f), "event")

    def close(self):
        self.tcp.close()
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description="Desktop GUI client for auction server")
    parser.add_argument("host", nargs="?", default="127.0.0.1")
    parser.add_argument("port", nargs="?", type=int, default=8080)
    args = parser.parse_args()
    root = tk.Tk()
    AuctionApp(root, args.host, args.port)
    root.mainloop()


if __name__ == "__main__":
    main()
