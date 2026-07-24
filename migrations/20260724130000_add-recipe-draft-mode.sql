-- Extends recipe_drafts to support the guided edit flow (/editrecipe).
-- `mode` distinguishes a create draft from an edit draft so the webhook can
-- apply the right semantics (edit preserves existing fields on "keep"). The
-- default 'create' keeps existing /newrecipe behavior unchanged.
-- `recipe_id` pins an edit draft to the exact recipe being edited, so a title
-- change during the flow still updates the original row rather than creating a
-- new one or matching a different recipe by title.
ALTER TABLE recipe_drafts ADD COLUMN IF NOT EXISTS mode TEXT NOT NULL DEFAULT 'create';
ALTER TABLE recipe_drafts ADD COLUMN IF NOT EXISTS recipe_id UUID;
