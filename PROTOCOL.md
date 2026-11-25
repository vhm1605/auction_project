# Application-layer protocol

Text-oriented TCP protocol, following the HW06 convention of command keywords,
three-digit response codes and CRLF line termination. This protocol replaces the
old OK/ERR/EVENT wire format; rebuild both client and server together.

- Request: `COMMAND [ARG1 ARG2 ...]\r\n` (uppercase keyword).
- Response/event: `NNN [FIELD1 FIELD2 ...]\r\n` (three ASCII digits; no symbolic name on the wire).
- A single ASCII space separates fields. Fields containing spaces, quotes or backslashes are enclosed in double quotes; inside quotes, `\"` and `\\` encode a quote and backslash. Empty fields use `""`. No pipe delimiters on the wire; existing input restrictions remain.
- UTF-8; maximum body length 2048 bytes excluding CRLF; output queue 64 KiB including CRLF.
- Strict CRLF framing: bare LF returns 453; oversized/malformed framing returns 452 and closes; invalid syntax/argument count returns 450.
- Events (1xx) are asynchronous. List headers 243/252 carry counts followed by 244/253 rows. No request ID; send commands in order.
- Client displays symbolic names locally for readability. The server remains authoritative for validation and auction state. Demo TCP is not encrypted.

## Requests

| Keyword | Arguments |
|---|---|
| HELP | none |
| EXIT | none |
| REGISTER | username, password |
| LOGIN | username, password |
| LOGOUT | none |
| CREATE_ROOM | room name |
| LIST_ROOMS | none |
| JOIN_ROOM | room ID |
| LEAVE_ROOM | none |
| ADD_ITEM | name, description, starting price, buy-now price |
| WITHDRAW_ITEM | item ID |
| LIST_ITEMS | none |
| START | none |
| BID | amount |
| BUY_NOW | none |

## Response codes

1xx connection notification and asynchronous events; 2xx success/data;
4xx business/authorization and syntax/data errors; 5xx internal/storage error.
3xx reserved (unused).

| Code | Message | Type |
|---|---|---|
| 100 | `CONNECTED` | INFO |
| 110 | `ITEM_STARTED` | EVENT |
| 111 | `MEMBER_JOINED` | EVENT |
| 112 | `MEMBER_LEFT` | EVENT |
| 113 | `ITEM_QUEUED` | EVENT |
| 114 | `ITEM_WITHDRAWN` | EVENT |
| 120 | `NEW_BID` | EVENT |
| 121 | `NOTICE` | EVENT |
| 130 | `SOLD` | EVENT |
| 131 | `QUEUE_EMPTY` | EVENT |
| 210 | `LOGGED_IN` | OK |
| 211 | `REGISTERED` | OK |
| 230 | `LOGGED_OUT` | OK |
| 231 | `BYE` | OK |
| 240 | `ROOM_CREATED` | OK |
| 241 | `JOINED_ROOM` | OK |
| 242 | `LEFT_ROOM` | OK |
| 243 | `ROOMS` | OK |
| 244 | `ROOM` | LIST DATA |
| 250 | `ITEM_QUEUED` | OK |
| 251 | `ITEM_WITHDRAWN` | OK |
| 252 | `ITEMS` | OK |
| 253 | `ITEM` | LIST DATA |
| 260 | `AUCTION_STARTED` | OK |
| 261 | `BID_ACCEPTED` | OK |
| 262 | `BOUGHT` | OK |
| 290 | `COMMANDS` | OK |
| 410 | `INVALID_CREDENTIALS` | ERR |
| 411 | `USERNAME_EXISTS` | ERR |
| 413 | `ALREADY_LOGGED_IN` | ERR |
| 414 | `USER_ALREADY_ONLINE` | ERR |
| 415 | `LOGOUT_FIRST` | ERR |
| 417 | `USER_LIMIT` | ERR |
| 421 | `LOGIN_REQUIRED` | ERR |
| 422 | `ONLY_OWNER_CAN_SELL` | ERR |
| 423 | `ONLY_OWNER_CAN_WITHDRAW` | ERR |
| 424 | `ONLY_OWNER_CAN_START` | ERR |
| 425 | `SELLER_CANNOT_BID` | ERR |
| 426 | `SELLER_CANNOT_BUY` | ERR |
| 430 | `ROOM_NOT_FOUND` | ERR |
| 431 | `ALREADY_IN_A_ROOM` | ERR |
| 432 | `NOT_IN_A_ROOM` | ERR |
| 433 | `JOIN_ROOM_FIRST` | ERR |
| 434 | `ROOM_LIMIT` | ERR |
| 435 | `ROOM_FULL` | ERR |
| 440 | `ITEM_NOT_FOUND` | ERR |
| 441 | `NO_ACTIVE_ITEM` | ERR |
| 442 | `BID_INVALID` | ERR |
| 443 | `ITEM_LIMIT` | ERR |
| 444 | `ONLY_QUEUED_ITEMS_CAN_BE_WITHDRAWN` | ERR |
| 445 | `AUCTION_ALREADY_ACTIVE` | ERR |
| 446 | `NO_QUEUED_ITEM` | ERR |
| 450 | `UNKNOWN_COMMAND_OR_WRONG_ARGUMENTS` | ERR |
| 451 | `BAD_CREDENTIAL_FORMAT` | ERR |
| 452 | `INVALID_OR_TOO_LONG_LINE` | ERR |
| 453 | `INVALID_LINE_ENDING` | ERR |
| 455 | `INVALID_ROOM_NAME` | ERR |
| 456 | `INVALID_ROOM_ID` | ERR |
| 457 | `INVALID_ITEM_TEXT` | ERR |
| 458 | `INVALID_PRICE` | ERR |
| 459 | `INVALID_ITEM_ID` | ERR |
| 501 | `STORAGE_FAILED` | ERR |

## Examples (wire format)

```text
S -> C: 100 "Send HELP for commands"\r\n
C -> S: LOGIN alice AlicePass123\r\n
S -> C: 210 alice\r\n
C -> S: CREATE_ROOM "Do dien tu"\r\n
S -> C: 240 1 "Do dien tu"\r\n
C -> S: ADD_ITEM Laptop "Da su dung" 100000 200000\r\n
S -> C: 250 1\r\n
S -> ALL: 113 1 1 Laptop\r\n
C -> S: BID 110000\r\n
S -> C: 261 1 110000\r\n
S -> ALL: 120 1 1 bob 110000\r\n
S -> ALL: 121 1 1 1/3 bob 110000\r\n```

Numeric code definitions and local display names remain in `include/protocol.h`.
