# Phân tích thiết kế - Đồ án Auction TCP

## 1. Đối chiếu theo 8 bài giảng

| Bài | Kiến thức giảng dạy liên quan | Cách áp dụng trong project |
|---|---|---|
| Lec01 - Network Programming / C review | cấu trúc dữ liệu, điều kiện, vòng lặp, hàm, I/O, linked list | struct `User/Room/Item/Client`, hàm tách phần việc, vòng lặp xử lý client |
| Lec02 - Internet Protocol and Services | TCP/UDP, ứng dụng mạng, giao thức ứng dụng | app sử dụng TCP reliable byte stream, định nghĩa request/reply/event |
| Lec03 - Socket API Introduction | stream sockets, `sockaddr_in`, socket address | `AF_INET`, `SOCK_STREAM`, `htons()`, `inet_pton()` |
| Lec04 - Elementary Sockets | `socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`, thiết kế protocol | server/client TCP theo đúng chuỗi API; protocol text line |
| Lec05 - Concurrent TCP Server | blocking/nonblocking, concurrent server, thread per client | server hỗ trợ đồng thời nhưng dùng `select`, thay vì mỗi client một thread |
| Lec06 - I/O Multiplexing Server | `select()`, `FD_ZERO`, `FD_SET`, `FD_ISSET`, timeval | theo dõi listening socket và nhiều client; lắng nghe cả sự kiện writable |
| Lec07 - Advanced I/O | nonblocking I/O, `fcntl`, `EAGAIN` | socket nonblocking; không bị kẹt khi một client không nhận dữ liệu |
| Lec08 - Broadcasting & Multicasting | phân biệt broadcast UDP so với TCP | **không dùng UDP `SO_BROADCAST` cho phiên đấu TCP**; thay bằng application-level fan-out đến thành viên trong phòng |

Điểm lấy từ **repo tham khảo** (`server-cpp/`): C++17, tổ chức `include/src/tools`, định nghĩa `data::User`, `network::send_all`, tách `server_state`, `persistence`, `logger`, **PostgreSQL qua libpq / persistence_pg.cpp**, server TCP và `CMakeLists.txt`.

Điểm **thay đổi để sát môn học**: dùng `select()` từ Lec06 thay cho `std::thread`/client trong repo; dùng dòng văn bản thay cho header 32 byte nhị phân; client terminal thay cho Android. Không cần Kubernetes/Android để demo; PostgreSQL là bắt buộc để lưu trạng thái bền vững. Không lặp lại một số rủi ro trong code tham khảo, chẳng hạn tin vào `userId` tự khai báo từ gói dữ liệu.

**Ngoại lệ ngoài slide**: OpenSSL PBKDF2 và `RAND_bytes` để lưu mật khẩu an toàn hơn. Không dùng thiết kế XOR giả AES trong phần crypto demo của repo.

## 2. Mô hình hệ thống

```text
  Client A (owner)       Client B (bidder)        Client C (bidder)
       |                       |                        |
       +----------- TCP ------+------------------------+
                               |
             +----------------------------------+
             | C++ server: select() event loop  |
             | listen_fd + multiple client_fds |
             | - parse protocol lines          |
             | - session authentication        |
             | - rooms and item FIFO queue     |
             | - bid/instant buy rules         |
             | - monotonic auction timer       |
             | - application-level broadcast   |
             +----------------------------------+
                         |               |
              PostgreSQL       activity.log
        users/rooms/items/bids  (bản sao nhật ký)
             /activity_logs
```

Mỗi người đăng nhập chỉ có một `socket_fd` và một `room_id`. Giá và trạng thái vật phẩm **chỉ thay đổi trong một event loop**, loại bỏ tranh chấp giữa các client dù nhiều socket cùng gửi BID tại một thời điểm. Client đăng nhập tự động rời phòng khi LOGOUT hoặc mất kết nối TCP.

### Luồng `recv`/`send`

```text
TCP bytes -> recv(fd, buf, 4096) -> append characters to input_line
                              | find '\n'
                              v
                    parse command + validation
                              v
                      business rule + state
                              v
                  queue replies and events per fd
                              v
                       select(writefds)
                              v
                   send(fd, partial bytes, MSG_NOSIGNAL)
```

Giới hạn: 2048 bytes mỗi lệnh; tối đa 64 KiB pending output mỗi client; 200 clients, 100 rooms, 100 items per room, 1000 accounts. Client quá chậm có thể bị ngắt để tránh chiếm RAM.

## 3. Trạng thái vật phẩm và thời gian

```text
           ADD_ITEM
              |
              v
           QUEUED ---- WITHDRAW_ITEM ----> WITHDRAWN
              |
        START / auto next
              |
              v
           ACTIVE ---- BUY_NOW ------> SOLD (instant)
              |
             BID
              |
         wait 180 seconds
              |
         NOTICE 1 at t+180
              |
         NOTICE 2 at t+210
              |
         NOTICE 3 at t+240
              |
              v
           SOLD (highest bid)
```

*BID mới ở bất kỳ mốc nào trước khi bán* sẽ đặt lại timer 180 giây và số thông báo về 0. Server sử dụng `steady_clock` để đo thời gian, không dựa vào việc chỉnh đồng hồ hệ thống. Nếu chưa có BID thì vật phẩm ACTIVE không tự kết thúc (đề bài chỉ đặt điều kiện kết thúc dựa trên lần BID gần nhất).

Cùng một phiên xử lý, nếu hai client cùng gửi BID, server xử lý lần lượt; người đến sau chỉ hợp lệ nếu giá mới lớn hơn giá hiện tại tối thiểu 10.000đ. `BUY_NOW` và kết thúc BID đều chỉ ghi SOLD một lần.

## 4. Yêu cầu secure coding

| Nội dung | Cách phòng tránh trong code |
|---|---|
| **Text-oriented protocol** | Mỗi câu lệnh kết thúc bằng CRLF, phân cách bằng dấu cách; không suy đoán `recv` = một message; UTF-8 validation cho room/item text |
| **Buffer overflow** | `std::string` với giới hạn rõ 2048 bytes; input > giới hạn bị từ chối; giới hạn 64 KiB hàng đợi gửi; không `strcpy`, không `gets` |
| **Integer overflow** | Parse chữ số từng ký tự, kiểm tra trước nhân 10 và cộng; dùng `int64_t` cho giá; mọi giá <= 9e15; kiểm tra trước khi +10000 |
| **Memory access safety** | tra `.find()` trước khi sử dụng map, không tin ID do client cung cấp, giới hạn `fd < FD_SETSIZE`, không giữ iterator bị invalidation khi đóng client |
| **Authorization** | server gắn socket với account sau LOGIN; chỉ owner được thêm/rút/START; chỉ thành viên phòng mới xem/bid/mua |
| **Session** | mỗi account 1 socket online, 1 room tại một thời điểm, logout/disconnect giải phóng room membership |
| **Resource exhaustion** | maximum users/rooms/items/clients; work quota mỗi lần đọc/accept; disconnect slow receivers |
| **Confidentiality of stored passwords** | OpenSSL PBKDF2-HMAC-SHA256 120k iterations, random 16-byte salt; OpenSSL constant-time comparison; chỉ hash/salt được lưu vào PostgreSQL |
| **Activity log** | bảng `activity_logs` trong PostgreSQL và file `activity.log` 0600; không ghi mật khẩu |

Lưu ý: không có TLS, brute-force protection, chống replay, thanh toán hoặc cơ chế phục hồi phiên đấu. Không thể coi đây là ứng dụng tài chính production.

## 5. Checklist chấm điểm (20 điểm)

| Mục | Điểm tối đa | Vị trí triển khai |
|---|---:|---|
| Xử lý truyền dòng | 1 | `server.cpp` -> `receive_from()`, `client.cpp` -> stdin/socket buffering |
| Cơ chế vào/ra socket server | 2 | `server.cpp` -> `run()`, `select()`, `send_to()` |
| Đăng ký/quản lý tài khoản | 2 | `REGISTER`; `persistence_pg.cpp` + PBKDF2 trong `persistence.cpp` |
| Đăng nhập/quản lý phiên | 2 | `LOGIN`, `LOGOUT`, `disconnect_client()` |
| Kiểm soát quyền phòng | 1 | `joined_room()`, `room->owner` checks |
| Tạo phòng đấu giá | 1 | `CREATE_ROOM` |
| Liệt kê phòng | 1 | `LIST_ROOMS` |
| Tạo vật phẩm đấu giá | 3 | `ADD_ITEM`, `START`, `start_next()` |
| Xóa/rút vật phẩm | 1 | `WITHDRAW_ITEM` (chỉ QUEUED) |
| Tham gia phòng | 2 | `JOIN_ROOM`, `LEAVE_ROOM`, check 1 room/user |
| Tố giá | 2 | `BID`, `check_timers()`, `finish_item()` |
| Mua trực tiếp | 1 | `BUY_NOW` |
| Log hoạt động | 1 | `logger.cpp` |

Đây là **bảng đối chiếu chức năng với rubric**, không phải lời cam kết chấm điểm thực tế của giảng viên.

## 6. Kịch bản trình diễn trước giảng viên

1. Mở server ở Terminal 1; mở 3 client ở Terminal 2/3/4.
2. Đăng ký + đăng nhập Alice, Bob, Carol; thử đăng nhập Bob lần thứ hai từ client khác và chứng minh bị từ chối.
3. Alice tạo room 1; Bob và Carol tham gia; Bob thử JOIN phòng khác (bị từ chối).
4. Bob thử ADD_ITEM và START (bị từ chối vì không là chủ phòng).
5. Alice ADD_ITEM 3 vật phẩm; WITHDRAW vật phẩm thứ 2 lúc còn QUEUED.
6. Alice START; thử WITHDRAW item đầu tiên (bị từ chối vì ACTIVE).
7. Bob BID dưới mức +10000 (bị từ chối), sau đó BID hợp lệ; xem broadcast ở các client.
8. Carol BID cao hơn; chạy timer demo 5s +2s, 3 NOTICE, SOLD; item 3 tự START.
9. Bob BUY_NOW item 3; xem SOLD ngay; thử BID thêm sau khi hết hàng.
10. Kiểm tra PostgreSQL (`users`, `rooms`, `items`, `bids`, `activity_logs`) và `activity.log`; restart server và xem các phòng/vật phẩm vẫn còn.

## 7. Kiểm thử đã thiết kế

`tests/integration_test.py` kiểm thử qua **socket TCP thực tế**, không chỉ gọi hàm trực tiếp. Gồm coalesced/fragmented messages, quyền truy cập, login duplicate, tăng giá 10k, integer quá lớn, buy now, ba mốc NOTICE, thứ tự FIFO, persist account/phòng/vật phẩm/giá, kiểm tra bảng PostgreSQL và không lưu plaintext password.

## 8. Terminal client

Client C++ sử dụng menu đánh số và nhập từng tham số. Client tự tạo lệnh TCP và dùng select() để nhận thông báo bất đồng bộ.

## 9. Lớp lưu trữ PostgreSQL (bổ sung)

```text
[C++ terminal client] -- TCP text --> [C++17 server: select()]
                                              |
                                        PostgreSQL libpq
                                              |
          +--------------+----------+---------+--------------+
          |              |          |         |              |
         users          rooms      items     bids       activity_logs
```

- `database/schema.sql`: tạo 5 bảng, PK/FK/CHECK constraints, unique partial index giới hạn 1 vật phẩm ACTIVE/phòng.
- `src/persistence_pg.cpp`: `PQconnectdb`, `PQexecParams` và các hàm `create_room`, `create_item`, `withdraw_item`, `save_bid`, `save_notice`, `finish_item`.
- Server **không cập nhật cache RAM nếu INSERT/UPDATE của hành động đó thất bại**. Các sự kiện quan trọng như BID + lịch sử giá, SOLD + kích hoạt hàng kế tiếp dùng SQL transaction.
- `load_state()` đọc `users`, `rooms`, `items` khi khởi động, khôi phục next ID bằng `max(id)+1`, khôi phục trạng thái ACTIVE và mốc NOTICE từ epoch milliseconds; tính thời gian trong phiên bằng `steady_clock`.
- Dùng một server duy nhất quản lý state để tránh các tiến trình khác nhau làm sai thứ tự giá đặt; PostgreSQL bảo vệ tính bền vững dữ liệu và ràng buộc, không thay thế cơ chế khóa phân tán trong mô hình nhiều server.
- `logger.cpp` vẫn ghi `activity.log` để phục vụ demo, đồng thời ghi PostgreSQL `activity_logs`. Nếu DB log riêng thất bại, server báo warning ở stderr; các phép cập nhật nghiệp vụ kiểm tra lỗi SQL trước khi thông báo thành công.
- Phiên TCP không được lưu vì socket descriptor không có ý nghĩa sau restart. Thành viên đăng nhập lại, JOIN lại phòng cũ.

### Kiểm thử mới

`tests/run_postgres_tests.py` tạo schema kiểm thử tạm, chạy `integration_test.py` qua TCP thật; truy vấn trực tiếp các bảng và dọn schema. Khi chỉ có libpq headers mà chưa có server PostgreSQL, mới kiểm tra được **biên dịch**, chưa thể xác nhận tích hợp DB runtime.


## Protocol status codes (current version)

The authoritative wire format is [PROTOCOL.md](PROTOCOL.md): space-separated keyword requests, numeric-only status code plus optional space-separated fields, quoted text, CRLF framing. `include/protocol.h` defines status codes. Any older framing examples above are superseded by this specification.


Current wire format: see PROTOCOL.md. No pipe separators or symbolic response names are transmitted; old examples are superseded.

Message classes: 1xx notification/event; 2xx success/data; 4xx client errors; 5xx server errors; 3xx reserved.
