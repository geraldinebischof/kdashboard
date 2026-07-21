DELETE FROM recipe_ingredients
WHERE recipe_id = (
  SELECT id FROM recipes WHERE title = 'Sample Protein Snack'
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
    '7bad2478-b0cc-4df9-a497-79fea6eeb449',
    'Sample Protein Snack',
    NULL,
    NULL,
    'Mix a high-protein batter with oil and seasoning, then steam or bake until set.'
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
    ('Protein flour', '80 g', 1),
    ('Yogurt', '60 g', 2),
    ('Oil', '0.5 tbsp', 3)
) AS ingredient(name, amount, sort_order);
