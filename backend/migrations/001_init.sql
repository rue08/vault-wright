-- Initial schema: users (mirrors Firebase-authenticated identities) and files
-- (file bytes stored directly in Postgres, no separate blob store).

CREATE TABLE IF NOT EXISTS users (
    id            serial PRIMARY KEY,
    email         text,
    firebase_uid  text UNIQUE NOT NULL,
    created_at    timestamptz DEFAULT now(),
    is_active     boolean DEFAULT true
);

CREATE TABLE IF NOT EXISTS files (
    id          serial PRIMARY KEY,
    user_id     integer REFERENCES users(id) ON DELETE CASCADE,
    filename    text,
    content     text,
    updated_at  timestamptz DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_files_user_id ON files(user_id);
