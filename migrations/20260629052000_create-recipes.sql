CREATE TABLE IF NOT EXISTS recipes (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  title TEXT NOT NULL UNIQUE CHECK (length(trim(title)) > 0),
  photo_url TEXT,
  photo_key TEXT,
  instructions TEXT NOT NULL DEFAULT '',
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS recipe_ingredients (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  recipe_id UUID NOT NULL REFERENCES recipes(id) ON DELETE CASCADE,
  name TEXT NOT NULL CHECK (length(trim(name)) > 0),
  amount TEXT NOT NULL CHECK (length(trim(amount)) > 0),
  sort_order INTEGER NOT NULL CHECK (sort_order > 0),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  CONSTRAINT recipe_ingredients_recipe_sort_key UNIQUE (recipe_id, sort_order)
);

CREATE INDEX IF NOT EXISTS recipe_ingredients_recipe_sort_idx
  ON recipe_ingredients (recipe_id, sort_order);

DROP TRIGGER IF EXISTS recipes_updated_at ON recipes;
CREATE TRIGGER recipes_updated_at
  BEFORE UPDATE ON recipes
  FOR EACH ROW
  EXECUTE FUNCTION system.update_updated_at();

DROP TRIGGER IF EXISTS recipe_ingredients_updated_at ON recipe_ingredients;
CREATE TRIGGER recipe_ingredients_updated_at
  BEFORE UPDATE ON recipe_ingredients
  FOR EACH ROW
  EXECUTE FUNCTION system.update_updated_at();

WITH recipe AS (
  INSERT INTO recipes (
    id,
    title,
    instructions
  )
  VALUES (
    'f4bf38e0-156f-466a-a459-52006d29d170',
    'Sample Breakfast Bowl',
    'Assemble fruit, yogurt, oats, seeds, and a crunchy topping in a bowl.'
  )
  ON CONFLICT (title) DO UPDATE
  SET instructions = EXCLUDED.instructions
  RETURNING id
),
cleared AS (
  DELETE FROM recipe_ingredients
  WHERE recipe_id = (SELECT id FROM recipe)
)
INSERT INTO recipe_ingredients (recipe_id, name, amount, sort_order)
SELECT recipe.id, ingredient.name, ingredient.amount, ingredient.sort_order
FROM recipe
CROSS JOIN (
  VALUES
    ('Fruit', '100 g', 1),
    ('Yogurt', '200 g', 2),
    ('Oats', '20 g', 3),
    ('Seeds', '1 tsp', 4),
    ('Crunchy topping', '10 g', 5)
) AS ingredient(name, amount, sort_order);
