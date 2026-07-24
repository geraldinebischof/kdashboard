-- Pending slash-command add flows for the Telegram bot. When a user taps a
-- bare /addgrocery, /addtodo, or /addchores (Telegram's / menu auto-sends on
-- tap), we store the intended list here and wait for the item text in the next
-- message. One row per chat — the bot is single-user.
CREATE TABLE IF NOT EXISTS pending_adds (
  chat_id TEXT PRIMARY KEY,
  list_key TEXT NOT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

ALTER TABLE pending_adds ENABLE ROW LEVEL SECURITY;
