-- init.sql
CREATE TABLE IF NOT EXISTS messages (
    id          SERIAL PRIMARY KEY,
    sender      VARCHAR(64)  NOT NULL,
    content     TEXT         NOT NULL,
    sent_at     TIMESTAMPTZ  NOT NULL DEFAULT NOW()
);
