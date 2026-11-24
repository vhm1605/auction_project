# AUCTION TCP - IT4062 (PostgreSQL + C++)

Dự án đấu giá trực tuyến theo 8 bài giảng Network Programming, dựa trên cách tổ chức `server-cpp/` trong GitHub https://github.com/tndat-dev/mobile-chatting-app.

**Công nghệ:** Server C++17, TCP `select()`/non-blocking I/O, PostgreSQL **qua `libpq`**, OpenSSL PBKDF2 cho mật khẩu; client C++ terminal với menu đánh số. Không cần Android Studio, Docker, Kubernetes. PostgreSQL phải hoạt động **trước khi** chạy server. Không có fallback sang lưu tài khoản bằng file.

## 1. Cài đặt Ubuntu / WSL Ubuntu

```bash
sudo apt update
sudo apt install -y build-essential cmake libssl-dev libpq-dev postgresql postgresql-client python3
sudo service postgresql start
```

Chạy các lệnh dưới bằng người dùng Ubuntu bình thường (không chạy project với `sudo`):

```bash
# Chạy một lần để tạo PostgreSQL role tương ứng với user Ubuntu hiện tại:
sudo -u postgres createuser --no-superuser --no-createdb --no-createrole "$(whoami)"
# Nếu báo role đã tồn tại, có thể bỏ qua.

# Tạo database cho riêng ứng dụng:
sudo -u postgres createdb --owner="$(whoami)" auction_db
# Nếu báo database đã tồn tại, kiểm tra trước khi dùng lại.

export DB_CONN_STR="host=/var/run/postgresql dbname=auction_db user=$(whoami)"
psql "$DB_CONN_STR" -v ON_ERROR_STOP=1 -f database/schema.sql
```

Nếu PostgreSQL trên máy dùng TCP/password thay vì peer authentication, thiết lập `DB_CONN_STR` tương ứng, ví dụ `host=127.0.0.1 port=5432 dbname=auction_db user=auction_app password=...`. **Không ghi password thật vào source code và không commit file chứa chuỗi kết nối**. `DB_CONN_STR` được đọc từ biến môi trường; có thể dùng `--db-conn` khi demo nhưng dễ lộ thông tin qua danh sách tiến trình.

## 2. Biên dịch và chạy

Trong thư mục chứa `CMakeLists.txt`:

```bash
cmake -S . -B build
cmake --build build -j4

# Terminal 1: Server C++ (DB_CONN_STR phải được export trong terminal này)
export DB_CONN_STR="host=/var/run/postgresql dbname=auction_db user=$(whoami)"
./build/auction_server --port 8080 --data-dir ./auction_data
```

Mở **terminal khác** tại thư mục dự án:

```bash
./build/auction_client 127.0.0.1 8080
```


**Demo không phải chờ 3 phút:**

```bash
./build/auction_server --port 8081 --data-dir ./demo_data --bid-timeout 5 --notice-gap 2
./build/auction_client 127.0.0.1 8081
```

Lưu ý: Thời gian 180/30 giây là **mặc định**; chỉ rút ngắn trong demo. Có thể mở nhiều terminal client với các tài khoản khác nhau.

## 3. Kiểm tra PostgreSQL đang lưu dữ liệu

```bash
psql "$DB_CONN_STR" -c 'SELECT username, created_at FROM users;'
psql "$DB_CONN_STR" -c 'SELECT id, name, owner FROM rooms ORDER BY id;'
psql "$DB_CONN_STR" -c 'SELECT id, name, status, highest_price, buyer, final_price FROM items ORDER BY id;'
psql "$DB_CONN_STR" -c 'SELECT item_id, bidder, amount, created_at FROM bids ORDER BY id;'
psql "$DB_CONN_STR" -c 'SELECT action, username, detail, created_at FROM activity_logs ORDER BY id DESC LIMIT 20;'
```

### Các bảng và chức năng

| Bảng | Dữ liệu |
|---|---|
| `users` | Username, salt, PBKDF2 password hash (không chứa mật khẩu thô) |
| `rooms` | ID phòng, tên phòng, chủ phòng |
| `items` | Hàng đợi FIFO, giá khởi điểm, giá mua ngay, giá cao nhất, buyer, trạng thái, deadline thông báo |
| `bids` | Lịch sử các lượt tố giá hợp lệ, người tố, giá và thời gian |
| `activity_logs` | Log REGISTER / LOGIN / JOIN / BID / NOTICE / SOLD và các sự kiện khác |

`database/schema.sql` tạo schema. `src/persistence_pg.cpp` dùng `PQconnectdb()`, `PQexecParams()`, `PQclear()`, `PQfinish()` tương tự hướng tiếp cận `persistence_pg.cpp` của GitHub. Các thay đổi nhiều bảng (BID + bid history; SOLD + chuyển vật phẩm tiếp) chạy trong `BEGIN`/`COMMIT` hoặc `ROLLBACK`. Để tránh SQL injection, **giá trị từ người dùng chỉ đưa vào parameter `$1`, `$2`..., không nối chuỗi SQL**.

**Khi khởi động lại server:** người dùng/phòng/vật phẩm/lịch sử tố giá/kết quả bán vẫn còn trong DB. Phiên đăng nhập và danh sách người đang ở trong phòng không tự phục hồi vì socket TCP cũ đã đóng; người dùng đăng nhập lại và JOIN phòng. Đồng hồ của vật phẩm đang bán có người đặt giá được phục hồi từ hạn thông báo lưu ở DB. Chỉ chạy **một phiên bản server** trên cùng database: trạng thái phiên và lịch đấu được điều phối trong một event loop, chưa thiết kế multi-server.

**Lưu dữ liệu cũ:** phiên bản này **không tự nhập** `accounts.tsv` của bản trước vào PostgreSQL. Nếu đã có tài khoản trong bản cũ, bạn phải nhập/chuyển đổi dữ liệu cũ riêng trước khi bỏ file.

## 4. Sử dụng giao diện

1. Mở client thứ nhất: đăng ký và đăng nhập `alice`, tạo một phòng, đăng hai hoặc ba vật phẩm (tên, mô tả, start price, buy now price).
2. Alice chọn Start auction; vật phẩm đầu tiên chuyển từ `QUEUED` sang `ACTIVE`.
3. Mở client thứ hai: đăng ký/đăng nhập `bob`, tham gia phòng Alice; xem giá hiện tại và các sự kiện trực tiếp.
4. Bob tố giá cao hơn ít nhất 10.000 VND. Mỗi lệnh hợp lệ reset timer về 180 giây.
5. Sau 180 giây không ai đặt giá mới, server gửi NOTICE 1; sau 30 giây gửi NOTICE 2; sau 30 giây gửi NOTICE 3 và chốt người thắng; vật phẩm kế tiếp tự giới thiệu.
6. Nếu người mua chọn **Buy now** trên vật phẩm đang ACTIVE, server chốt ngay ở giá buy now.

Chỉ người tạo phòng được đăng/rút vật phẩm hoặc bắt đầu phiên. Chỉ có thể rút vật phẩm `QUEUED`. Một người chỉ ở một phòng tại một thời điểm; mỗi tài khoản chỉ đăng nhập được một phiên đồng thời.

## 5. Giao thức TCP - text oriented

Gửi các dòng `COMMAND|ARG1|ARG2\n`. Server trả `OK|...`, `ERR|...`, `EVENT|...`, hoặc danh sách `ROOM|...`, `ITEM|...`. Client sử dụng menu đánh số. Các lệnh TCP tương ứng:

```text
REGISTER|alice|AlicePass123
LOGIN|alice|AlicePass123
CREATE_ROOM|Công nghệ
ADD_ITEM|Laptop|Máy cũ còn tốt|100000|200000
START
LIST_ITEMS
```

Khách mua:

```text
REGISTER|bob|BobPass1234
LOGIN|bob|BobPass1234
LIST_ROOMS
JOIN_ROOM|1
BID|110000
BUY_NOW
```

Các lệnh bổ sung: `HELP`, `LOGOUT`, `CHANGE_PASSWORD|old|new`, `WHOAMI`, `JOIN_ROOM|id`, `LEAVE_ROOM`, `LIST_ROOMS`, `LIST_ITEMS`, `WITHDRAW_ITEM|id`, `EXIT`.

**TCP là byte stream**, không coi một `recv()` là một lệnh. Server tích lũy dữ liệu cho tới LF; cắt tối đa 2048 bytes mỗi dòng, kiểm UTF-8, giới hạn 64 KiB output/client và kiểm số nguyên trước khi cộng/nhân. Dùng `int64_t` cho giá VND; không dùng `float`.

## 6. Kiểm thử

Có script `tests/run_postgres_tests.py` tạo **schema tạm riêng** trên một database kiểm thử được chỉ định, nạp schema và chạy kiểm thử socket nhiều client; kiểm tra tiếp bảng SQL rồi xóa schema tạm.

```bash
# Tạo DB test chỉ một lần:
sudo -u postgres createdb --owner="$(whoami)" auction_test_db

# Trong terminal có psql, server đã được build:
export TEST_DB_CONN_STR="host=/var/run/postgresql dbname=auction_test_db user=$(whoami)"
python3 tests/run_postgres_tests.py
```


Tập kiểm thử có các tình huống gói TCP bị chia/gộp, quyền truy cập, giới hạn số, tăng giá 10.000đ, bộ đếm bị reset sau BID, 3 NOTICE, thứ tự hàng đợi, tranh chấp BUY_NOW, khởi động lại server và truy vấn dữ liệu SQL.

## 7. Cấu trúc

```text
auction_project/
├── CMakeLists.txt
├── database/schema.sql
├── include/{server_state.h,network_utils.h,persistence.h,persistence_pg.h,logger.h}
├── src/{server.cpp,network_utils.cpp,persistence.cpp,persistence_pg.cpp,logger.cpp}
├── tools/client.cpp
├── tests/{integration_test.py,run_postgres_tests.py}
├── DESIGN.md
└── TEST_RESULTS.md
```

## 8. Giới hạn an toàn

- Mật khẩu lưu bằng salt ngẫu nhiên + PBKDF2-HMAC-SHA256; giao thức TCP demo **không có TLS**, nên chỉ chạy localhost/LAN tin cậy, không công khai ra Internet.
- SQL dùng câu lệnh tham số hóa. Chức năng mua/bán chỉ **xác định người thắng và giá**, không chuyển tiền thật.
- Kết nối libpq dùng I/O đồng bộ, có thể làm event loop tạm dừng khi PostgreSQL chậm. Phù hợp demo một server/DB nội bộ, không phải hệ thống production.
- `activity.log` vẫn được lưu ở `--data-dir` như bản cũ để dễ chấm phần log; bảng `activity_logs` trong PostgreSQL cũng nhận bản ghi. Tài khoản và trạng thái đấu giá **không còn lưu file TSV**.
