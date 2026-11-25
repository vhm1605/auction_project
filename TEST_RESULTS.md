# Kết quả kiểm tra bản PostgreSQL

## 1. Kiểm tra đã thực hiện trong môi trường tạo mã

- `cmake -S . -B build`: **PASS**; CMake tìm thấy `libpq` PostgreSQL 17.11 và OpenSSL.
- `cmake --build build -j4`: **PASS**; biên dịch được `auction_server`, `auction_client` với C++17 và cờ cảnh báo `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`.
- `python3 -m py_compile tests/integration_test.py tests/run_postgres_tests.py`: **PASS**; cú pháp Python hợp lệ.
- Kiểm tra schema và đường gọi PostgreSQL: `database/schema.sql`, `src/persistence_pg.cpp`, các lệnh query parameterized và SQL transaction đã được thêm vào code.

## 2. Chưa thể xác nhận trong môi trường này

Môi trường tạo mã có **thư viện phát triển libpq** nhưng **không có tiến trình PostgreSQL server + psql**. Do đó **chưa chạy được** bài test end-to-end với database thật; không được coi PASS biên dịch là PASS integration runtime.

Hãy chạy trên Ubuntu/WSL có PostgreSQL theo `README.md`, rồi:

```bash
export TEST_DB_CONN_STR="host=/var/run/postgresql dbname=auction_test_db user=$(whoami)"
python3 tests/run_postgres_tests.py
```

Bộ kiểm thử tự động gồm:

1. REGISTER/LOGIN và kiểm tra không đăng nhập hai phiên.
2. Tạo phòng, một người một phòng, phân quyền chủ phòng.
3. ADD_ITEM / FIFO / WITHDRAW_ITEM.
4. BID tăng tối thiểu 10.000 VND, số cực lớn, quyền của người bán.
5. NOTICE 1/2/3, BID mới đặt lại thời gian.
6. BUY_NOW, tranh chấp hai người mua đồng thời.
7. TCP fragmentation/coalescing và dòng quá dài.
8. Dừng server và khởi động lại: users/rooms/items và kết quả bán vẫn tồn tại.
9. Dùng `psql` kiểm tra dữ liệu `users`, `rooms`, `items`, `bids`, `activity_logs`.

**Cần kiểm thử thêm trước khi triển khai thực tế:** mất kết nối PostgreSQL giữa transaction, load lớn, khôi phục khi thay đổi giờ hệ thống, đa tiến trình server, bảo mật đường truyền TCP. Đây là đồ án học tập, không phải dịch vụ thanh toán công khai.

Lưu ý: Kết quả trên thuộc phiên bản trước; cần kiểm thử lại menu client mới.


## Protocol update

The integration tests now check numeric status codes, CRLF, invalid commands and fragmented/coalesced lines. Prior test results refer to the old protocol; rerun build and PostgreSQL integration tests for this version.


## Space-delimited protocol update

Rebuild and rerun integration tests. Historical results do not validate this revision.

Cần build và chạy lại integration test sau khi bỏ WHOAMI và CHANGE_PASSWORD.

## Message code regrouping
1xx notifications/events; 2xx success/data; 4xx client errors; 5xx server errors. Rebuild both binaries and rerun PostgreSQL integration tests; previous results do not validate this revision.
