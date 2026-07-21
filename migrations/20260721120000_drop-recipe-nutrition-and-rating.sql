-- Drop recipe nutrition and rating columns.
-- Schema migrations in this project are re-applied by scripts/bootstrap-insforge-kit.mjs
-- on every run, and the historical create-recipes / sample-data migrations have been
-- edited to omit these columns. This migration is the cleanup path for databases that
-- were bootstrapped before the removal: each DROP COLUMN IF EXISTS is idempotent and
-- safe to re-run on already-cleaned databases.
ALTER TABLE recipes
  DROP COLUMN IF EXISTS total_calories,
  DROP COLUMN IF EXISTS carbs_g,
  DROP COLUMN IF EXISTS fat_g,
  DROP COLUMN IF EXISTS protein_g,
  DROP COLUMN IF EXISTS rating;

ALTER TABLE recipe_ingredients
  DROP COLUMN IF EXISTS calories;
