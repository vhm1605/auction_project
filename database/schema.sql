-- Auction schema for PostgreSQL (single auction server process).
-- Run once with: psql "$DB_CONN_STR" -v ON_ERROR_STOP=1 -f database/schema.sql
CREATE TABLE IF NOT EXISTS users (
    username VARCHAR(24) PRIMARY KEY,
    salt_hex CHAR(32) NOT NULL,
    password_hash_hex CHAR(64) NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    CONSTRAINT valid_username CHECK (username ~ '^[A-Za-z0-9_]{3,24}$'),
    CONSTRAINT valid_salt CHECK (salt_hex ~ '^[0-9a-f]{32}$'),
    CONSTRAINT valid_hash CHECK (password_hash_hex ~ '^[0-9a-f]{64}$')
);

CREATE TABLE IF NOT EXISTS rooms (
    id INTEGER PRIMARY KEY CHECK (id BETWEEN 1 AND 999999),
    name VARCHAR(64) NOT NULL,
    owner VARCHAR(24) NOT NULL REFERENCES users(username),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS items (
    id INTEGER PRIMARY KEY CHECK (id BETWEEN 1 AND 999999),
    room_id INTEGER NOT NULL REFERENCES rooms(id),
    name VARCHAR(80) NOT NULL,
    description VARCHAR(240) NOT NULL,
    seller VARCHAR(24) NOT NULL REFERENCES users(username),
    start_price BIGINT NOT NULL CHECK (start_price > 0),
    buy_now_price BIGINT NOT NULL CHECK (buy_now_price >= start_price + 10000),
    highest_price BIGINT NOT NULL CHECK (highest_price >= start_price),
    highest_bidder VARCHAR(24) REFERENCES users(username),
    buyer VARCHAR(24) REFERENCES users(username),
    final_price BIGINT NOT NULL DEFAULT 0 CHECK (final_price >= 0),
    status VARCHAR(12) NOT NULL DEFAULT 'QUEUED' CHECK (status IN ('QUEUED','ACTIVE','SOLD','WITHDRAWN')),
    notifications SMALLINT NOT NULL DEFAULT 0 CHECK (notifications BETWEEN 0 AND 3),
    next_notice_ms BIGINT NOT NULL DEFAULT 0 CHECK (next_notice_ms >= 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    sold_at TIMESTAMPTZ,
    CONSTRAINT valid_sale CHECK ((status = 'SOLD' AND buyer IS NOT NULL AND final_price > 0) OR
                                 (status <> 'SOLD' AND buyer IS NULL AND final_price = 0))
);
-- At most one ACTIVE item per room, including across server restarts.
CREATE UNIQUE INDEX IF NOT EXISTS one_active_item_per_room ON items(room_id) WHERE status = 'ACTIVE';
CREATE INDEX IF NOT EXISTS items_room_queue ON items(room_id, id);

CREATE TABLE IF NOT EXISTS bids (
    id BIGSERIAL PRIMARY KEY,
    item_id INTEGER NOT NULL REFERENCES items(id),
    bidder VARCHAR(24) NOT NULL REFERENCES users(username),
    amount BIGINT NOT NULL CHECK (amount > 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS bids_item_history ON bids(item_id, id);

CREATE TABLE IF NOT EXISTS activity_logs (
    id BIGSERIAL PRIMARY KEY,
    action VARCHAR(40) NOT NULL,
    username VARCHAR(24) NOT NULL,
    detail VARCHAR(512) NOT NULL DEFAULT '',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
