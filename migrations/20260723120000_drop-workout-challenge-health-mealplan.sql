-- Drop the four retired features: workout/meal planner lists, 75 Hard
-- challenge, health targets, health daily summaries, and meal plan.
-- The cookbook (recipes/recipe_ingredients) and the remaining planner lists
-- (grocery/todo/daily_chores) are unaffected.
DROP TABLE IF EXISTS meal_plan_entries;
DROP TABLE IF EXISTS challenge_daily_logs;
DROP TABLE IF EXISTS health_targets;
DROP TABLE IF EXISTS health_daily_summaries;

-- Retire the workout and meal list rows. planner_lists/planner_items stay.
DELETE FROM planner_items WHERE list_key IN ('workout', 'meal');
DELETE FROM planner_lists WHERE key IN ('workout', 'meal');

-- Narrow the planner_lists CHECK to the keys that remain.
ALTER TABLE planner_lists DROP CONSTRAINT IF EXISTS planner_lists_key_check;
ALTER TABLE planner_lists ADD CONSTRAINT planner_lists_key_check
  CHECK (key IN ('grocery', 'todo', 'daily_chores'));
