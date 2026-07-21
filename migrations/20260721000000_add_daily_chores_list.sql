-- Add a third planner list: "daily_chores" (DAILY CHORES panel). Unlike todo
-- items (which are deleted the day after completion), daily-chores items recur:
-- they are reset to not-done at the start of each local day, never deleted.

-- Widen the key CHECK constraint to allow the new key. The constraint name is
-- the Postgres default (<table>_<column>_check).
ALTER TABLE planner_lists DROP CONSTRAINT IF EXISTS planner_lists_key_check;
ALTER TABLE planner_lists
  ADD CONSTRAINT planner_lists_key_check
  CHECK (key IN ('grocery', 'workout', 'meal', 'todo', 'daily_chores'));

-- Seed the new list. sort_order 5 is free; on-screen position is driven by the
-- payload array in kindle-dashboard-data.ts, not by sort_order.
INSERT INTO planner_lists (key, title, sort_order)
VALUES ('daily_chores', 'Daily Chores', 5)
ON CONFLICT (key) DO UPDATE
SET title = EXCLUDED.title,
    sort_order = EXCLUDED.sort_order;
