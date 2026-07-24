-- Generalizes pending_adds so it can also stash a pending recipe command
-- (/recipe, /editrecipe, /deleterecipe) awaiting its title — the same
-- tap-then-type pattern the planner add commands already use. Telegram's /
-- menu auto-sends a bare command on tap, so the webhook stores it here and
-- routes the next plain-text message to it. One row per chat.
ALTER TABLE pending_adds ADD COLUMN IF NOT EXISTS command TEXT;
ALTER TABLE pending_adds ALTER COLUMN list_key DROP NOT NULL;
