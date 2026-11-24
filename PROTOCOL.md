# Application-layer protocol

Text-oriented TCP protocol, following the HW06 convention of command keywords,
three-digit response codes and CRLF line termination. This protocol replaces the
old OK/ERR/EVENT wire format; rebuild both client and server together.

- Request: `COMMAND[|ARG1|ARG2...]\r\n` (uppercase keyword identifies the request).
- Response/event: `NNN MESSAGE[|FIELD1|FIELD2...]\r\n` (three ASCII digits, one space, symbolic name, optional pipe-separated fields).
- `\r\n` denotes actual CR (0x0D) then LF (0x0A), not four literal characters.
- UTF-8 text; spaces in names/descriptions are preserved. Pipe and control characters are not permitted inside user fields. Maximum body size is 2048 bytes, excluding CRLF. Output queue limit is 64 KiB per client, including CRLF.
- A request is processed only after CRLF. Bare LF returns 303; malformed/oversized input returns 302 and closes the connection. Wrong keyword or argument count returns 300. TCP fragmentation/coalescing does not change message boundaries.
- Server events (4xx) can arrive independently of a request or interleave with its response. List headers 143/152 carry counts followed by that many 144/153 rows. No request ID is used; send commands in order.
- The client menu builds request lines automatically. The server remains authoritative for validation, authorization and auction state.
- Demo TCP is not encrypted; do not transmit real passwords over untrusted networks.

## Requests

| Keyword | Arguments |
|---|---|
| HELP | none |
| EXIT | none |
| REGISTER | username, password |
| LOGIN | username, password |
| LOGOUT | none |
| CHANGE_PASSWORD | old password, new password |
| WHOAMI | none |
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

1xx success/data; 2xx business/authorization error; 3xx syntax/data error;
4xx asynchronous auction/room event; 5xx internal/storage error.

| Code | Message | Type |
|---|---|---|
| 100 | `CONNECTED` | OK |
| 110 | `LOGGED_IN` | OK |
| 111 | `REGISTERED` | OK |
| 112 | `PASSWORD_CHANGED` | OK |
| 113 | `SESSION` | OK |
| 130 | `LOGGED_OUT` | OK |
| 131 | `BYE` | OK |
| 140 | `ROOM_CREATED` | OK |
| 141 | `JOINED_ROOM` | OK |
| 142 | `LEFT_ROOM` | OK |
| 143 | `ROOMS` | OK |
| 144 | `ROOM` | LIST DATA |
| 150 | `ITEM_QUEUED` | OK |
| 151 | `ITEM_WITHDRAWN` | OK |
| 152 | `ITEMS` | OK |
| 153 | `ITEM` | LIST DATA |
| 160 | `AUCTION_STARTED` | OK |
| 161 | `BID_ACCEPTED` | OK |
| 162 | `BOUGHT` | OK |
| 190 | `COMMANDS` | OK |
| 210 | `INVALID_CREDENTIALS` | ERR |
| 211 | `USERNAME_EXISTS` | ERR |
| 213 | `ALREADY_LOGGED_IN` | ERR |
| 214 | `USER_ALREADY_ONLINE` | ERR |
| 215 | `LOGOUT_FIRST` | ERR |
| 216 | `INVALID_OLD_PASSWORD` | ERR |
| 217 | `USER_LIMIT` | ERR |
| 221 | `LOGIN_REQUIRED` | ERR |
| 222 | `ONLY_OWNER_CAN_SELL` | ERR |
| 223 | `ONLY_OWNER_CAN_WITHDRAW` | ERR |
| 224 | `ONLY_OWNER_CAN_START` | ERR |
| 225 | `SELLER_CANNOT_BID` | ERR |
| 226 | `SELLER_CANNOT_BUY` | ERR |
| 230 | `ROOM_NOT_FOUND` | ERR |
| 231 | `ALREADY_IN_A_ROOM` | ERR |
| 232 | `NOT_IN_A_ROOM` | ERR |
| 233 | `JOIN_ROOM_FIRST` | ERR |
| 234 | `ROOM_LIMIT` | ERR |
| 235 | `ROOM_FULL` | ERR |
| 240 | `ITEM_NOT_FOUND` | ERR |
| 241 | `NO_ACTIVE_ITEM` | ERR |
| 242 | `BID_INVALID` | ERR |
| 243 | `ITEM_LIMIT` | ERR |
| 244 | `ONLY_QUEUED_ITEMS_CAN_BE_WITHDRAWN` | ERR |
| 245 | `AUCTION_ALREADY_ACTIVE` | ERR |
| 246 | `NO_QUEUED_ITEM` | ERR |
| 300 | `UNKNOWN_COMMAND_OR_WRONG_ARGUMENTS` | ERR |
| 301 | `BAD_CREDENTIAL_FORMAT` | ERR |
| 302 | `INVALID_OR_TOO_LONG_LINE` | ERR |
| 303 | `INVALID_LINE_ENDING` | ERR |
| 304 | `INVALID_NEW_PASSWORD` | ERR |
| 305 | `INVALID_ROOM_NAME` | ERR |
| 306 | `INVALID_ROOM_ID` | ERR |
| 307 | `INVALID_ITEM_TEXT` | ERR |
| 308 | `INVALID_PRICE` | ERR |
| 309 | `INVALID_ITEM_ID` | ERR |
| 400 | `ITEM_STARTED` | EVENT |
| 401 | `MEMBER_JOINED` | EVENT |
| 402 | `MEMBER_LEFT` | EVENT |
| 403 | `ITEM_QUEUED` | EVENT |
| 404 | `ITEM_WITHDRAWN` | EVENT |
| 410 | `NEW_BID` | EVENT |
| 411 | `NOTICE` | EVENT |
| 420 | `SOLD` | EVENT |
| 421 | `QUEUE_EMPTY` | EVENT |
| 500 | `HASH_FAILED` | ERR |
| 501 | `STORAGE_FAILED` | ERR |

## Examples (wire format)

```text
S -> C: 100 CONNECTED|Send HELP for commands\r\n
C -> S: LOGIN|alice|AlicePass123\r\n
S -> C: 110 LOGGED_IN|alice\r\n
C -> S: CREATE_ROOM|Do dien tu\r\n
S -> C: 140 ROOM_CREATED|1|Do dien tu\r\n
C -> S: ADD_ITEM|Laptop|Da su dung|100000|200000\r\n
S -> C: 150 ITEM_QUEUED|1\r\n
S -> C: 403 ITEM_QUEUED|1|1|Laptop\r\n
```

Example for a buyer in room 1 with item 1 active:

```text
C -> S: BID|110000\r\n
S -> C: 161 BID_ACCEPTED|1|110000\r\n
S -> C: 410 NEW_BID|1|1|bob|110000\r\n
S -> C: 411 NOTICE|1|1|1/3\r\n
```

The numeric status codes are defined in `include/protocol.h`. Existing message
names and payload fields are retained, except the generic OK/ERR/EVENT prefix
is replaced by the specific numeric code. Invalid username/password is deliberately
reported together as 210, avoiding disclosure of account existence.
