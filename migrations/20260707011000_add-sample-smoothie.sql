DELETE FROM recipe_ingredients
WHERE recipe_id = (
  SELECT id FROM recipes WHERE title = 'Sample Smoothie'
);

WITH recipe AS (
  INSERT INTO recipes (
    id,
    title,
    photo_url,
    photo_key,
    instructions
  )
  VALUES (
    '3202ae4d-131a-4ef1-8bee-887cff72ac5a',
    'Sample Smoothie',
    NULL,
    NULL,
    'Blend milk, fruit, flavoring, and a crunchy add-in into a cold smoothie.'
  )
  ON CONFLICT (title) DO UPDATE
  SET photo_url = EXCLUDED.photo_url,
      photo_key = EXCLUDED.photo_key,
      instructions = EXCLUDED.instructions
  RETURNING id
)
INSERT INTO recipe_ingredients (recipe_id, name, amount, sort_order)
SELECT recipe.id, ingredient.name, ingredient.amount, ingredient.sort_order
FROM recipe
CROSS JOIN (
  VALUES
    ('Milk', '200 ml', 1),
    ('Fruit', '0.5 cup', 2),
    ('Flavoring', '1 tsp', 3),
    ('Crunchy add-in', '10 g', 4)
) AS ingredient(name, amount, sort_order);
