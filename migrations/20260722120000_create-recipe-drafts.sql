-- In-progress recipe drafts for the Telegram chat-form recipe flow
-- (/newrecipe). One row per chat: the bot is single-user, so at most one
-- active draft per chat at a time.
CREATE TABLE IF NOT EXISTS recipe_drafts (
  chat_id TEXT PRIMARY KEY,
  title TEXT NOT NULL DEFAULT '',
  ingredients JSONB NOT NULL DEFAULT '[]'::jsonb,
  instructions TEXT NOT NULL DEFAULT '',
  stage TEXT NOT NULL DEFAULT 'title',
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

ALTER TABLE recipe_drafts ENABLE ROW LEVEL SECURITY;

DROP TRIGGER IF EXISTS recipe_drafts_updated_at ON recipe_drafts;
CREATE TRIGGER recipe_drafts_updated_at
  BEFORE UPDATE ON recipe_drafts
  FOR EACH ROW
  EXECUTE FUNCTION system.update_updated_at();
