DELETE FROM recipe_ingredients
WHERE recipe_id = (
  SELECT id FROM recipes WHERE title = 'Sample Toast'
);

WITH recipe AS (
  INSERT INTO recipes (
    title,
    photo_url,
    photo_key,
    instructions
  )
  VALUES (
    'Sample Toast',
    NULL,
    NULL,
    'Toast bread with a protein filling, cheese, vegetables, and a savory spread until warmed through.'
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
    ('Bread', '2 slices', 1),
    ('Protein filling', '100 g', 2),
    ('Cheese', '20 g', 3),
    ('Vegetables', '50 g', 4),
    ('Savory spread', '1 tbsp', 5)
) AS ingredient(name, amount, sort_order);
