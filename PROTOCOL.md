# Auction application protocol (compact)

TCP, UTF-8, CRLF (`\r\n`). Request: `COMMAND [fields...]`. Response/event: `CODE MESSAGE [fields...]`. One fixed message name per code. Error descriptions are quoted human-readable Vietnamese text, not additional identifiers. Fields containing spaces, quotes or backslashes use the existing quoting and escaping rules. Pipes are not allowed in wire fields. Rebuild server and client together.

## Requests and successful responses

| Request | Success response |
|---|---|
| `REGISTER username password` | `200 OK REGISTER username` |
| `LOGIN username password` | `200 OK LOGIN username` |
| `LOGOUT` | `200 OK LOGOUT` |
| `CREATE_ROOM room_name` | `200 OK CREATE_ROOM room_id room_name` |
| `LIST_ROOMS` | `200 OK LIST_ROOMS room_count` |
| `JOIN_ROOM room_id` | `200 OK JOIN_ROOM room_id room_name` |
| `LEAVE_ROOM` | `200 OK LEAVE_ROOM` |
| `ADD_ITEM name description start_price buy_now_price` | `200 OK ADD_ITEM item_id` |
| `WITHDRAW_ITEM item_id` | `200 OK WITHDRAW_ITEM item_id` |
| `LIST_ITEMS` | `200 OK LIST_ITEMS item_count ACTIVE=active_item_id` |
| `START` | `200 OK START` |
| `BID amount` | `200 OK BID item_id amount` |
| `BUY_NOW` | `200 OK BUY_NOW item_id amount` |
| `HELP` | `200 OK HELP command1 command2 ...` |
| `EXIT` | `200 OK EXIT` |

LIST_ROOMS and LIST_ITEMS send one data message per row after the count response. HELP lists all 15 requests. EXIT sends OK before closing.

## Event and data formats

- `100 CONNECTED message`
- `110 ITEM_STARTED room_id item_id name description start_price buy_now_price`
- `111 MEMBER_EVENT room_id JOIN/LEAVE username`
- `113 ITEM_EVENT room_id ADD/WITHDRAW item_id [name]`
- `120 NEW_BID room_id item_id bidder amount`
- `121 CLOSING_NOTICE room_id item_id notice_count highest_bidder highest_price`
- `130 ITEM_SOLD room_id item_id buyer final_price mode`
- `131 QUEUE_EMPTY room_id`
- `200 OK COMMAND [fields...]`
- `201 ROOM_DATA LIST_ROOMS room_id room_name owner member_count`
- `202 ITEM_DATA LIST_ITEMS item_id status name description start_price buy_now_price highest_price highest_bidder buyer`

JOIN/LEAVE and ADD/WITHDRAW are alternative literal actions; name is present only for ADD. Sale mode is BID or BUY_NOW. Broadcasts are scoped to room members.

## Errors

Format: `CODE MESSAGE "DESCRIPTION"`.

| Code | Message | Description |
|---|---|---|
| 400 | INVALID_REQUEST | Message không hợp lệ hoặc quá dài |
| 400 | INVALID_REQUEST | Sai ký tự kết thúc dòng |
| 400 | INVALID_REQUEST | Sai tên lệnh hoặc số tham số |
| 401 | AUTH_ERROR | Phiên đã đăng nhập |
| 401 | AUTH_ERROR | Phải đăng nhập trước |
| 401 | AUTH_ERROR | Phải đăng xuất trước |
| 401 | AUTH_ERROR | Sai tài khoản hoặc mật khẩu |
| 401 | AUTH_ERROR | Tài khoản đang đăng nhập nơi khác |
| 401 | AUTH_ERROR | Tài khoản đã tồn tại |
| 403 | FORBIDDEN | Chỉ chủ phòng được bắt đầu đấu giá |
| 403 | FORBIDDEN | Chỉ chủ phòng được rút vật phẩm |
| 403 | FORBIDDEN | Chỉ chủ phòng được thêm vật phẩm |
| 403 | FORBIDDEN | Người bán không được tự mua |
| 403 | FORBIDDEN | Người bán không được tự đặt giá |
| 404 | NOT_FOUND | Không tìm thấy phòng |
| 404 | NOT_FOUND | Không tìm thấy vật phẩm |
| 409 | INVALID_STATE | Chưa tham gia phòng |
| 409 | INVALID_STATE | Không có vật phẩm chờ |
| 409 | INVALID_STATE | Không có vật phẩm đang đấu giá |
| 409 | INVALID_STATE | Vật phẩm không ở trạng thái chờ |
| 409 | INVALID_STATE | Đang có vật phẩm đấu giá |
| 409 | INVALID_STATE | Đang ở trong phòng |
| 422 | INVALID_DATA | Giá không hợp lệ |
| 422 | INVALID_DATA | Giá đặt không hợp lệ |
| 422 | INVALID_DATA | ID phòng không hợp lệ |
| 422 | INVALID_DATA | ID vật phẩm không hợp lệ |
| 422 | INVALID_DATA | Sai định dạng tài khoản |
| 422 | INVALID_DATA | Tên hoặc mô tả không hợp lệ |
| 422 | INVALID_DATA | Tên phòng không hợp lệ |
| 429 | LIMIT_REACHED | Phòng đã đầy |
| 429 | LIMIT_REACHED | Đạt giới hạn số phòng |
| 429 | LIMIT_REACHED | Đạt giới hạn tài khoản |
| 429 | LIMIT_REACHED | Đạt giới hạn vật phẩm |
| 501 | STORAGE_FAILED | Lỗi lưu trữ dữ liệu |

19 distinct response/event codes; 15 requests. Auction business rules and database schema unchanged.
