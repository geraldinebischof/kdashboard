import { createAdminClient } from "npm:@insforge/sdk";

type ListKey = "grocery" | "todo" | "daily_chores";

type PlannerItem = {
  id: string;
  list_key: ListKey;
  text: string;
  done: boolean;
  created_at: string;
  updated_at: string;
};

type RecipeRow = {
  id: string;
  title: string;
  photo_url: string | null;
  photo_key: string | null;
  instructions: string;
  created_at: string;
  updated_at: string;
};

type RecipeIngredientRow = {
  recipe_id: string;
  name: string;
  amount: string;
  sort_order: number;
};

type RecipePayload = {
  id: string;
  title: string;
  photo_url: string | null;
  photo_key: string | null;
  instructions: string;
  ingredients: Array<{
    name: string;
    amount: string;
    sort_order: number;
  }>;
};

type DashboardPayload = {
  ok: true;
  generated_at: string;
  version: string;
  lists: Array<{
    key: ListKey;
    title: string;
    items: Array<{
      id: string;
      text: string;
      done: boolean;
      updated_at: string;
    }>;
  }>;
  recipes: RecipePayload[];
};

const LIST_TITLES: Record<ListKey, string> = {
  todo: "To Do",
  grocery: "Grocery",
  daily_chores: "Daily Chores"
};
const COMPLETED_ITEM_HIDE_AFTER_MS = 24 * 60 * 60 * 1000;

export default async function(req: Request): Promise<Response> {
  const requestStarted = timeMs();
  if (req.method === "OPTIONS") {
    return new Response(null, { status: 204, headers: corsHeaders() });
  }

  if (req.method !== "GET") {
    return jsonResponse({ ok: false, error: "Method not allowed" }, 405);
  }

  if (!isAuthorizedDashboardRead(req)) {
    return jsonResponse({ ok: false, error: "Unauthorized" }, 401);
  }

  try {
    const payload = await loadDashboardPayload();
    return jsonResponse(payload);
  } catch (error) {
    return jsonResponse({ ok: false, error: errorMessage(error) }, 500);
  } finally {
    logTiming("kindle-dashboard-data", { total_ms: elapsedMs(requestStarted) });
  }
}

async function loadDashboardPayload(): Promise<DashboardPayload> {
  const admin = createAdminClient({
    baseUrl: requiredEnv("INSFORGE_BASE_URL"),
    apiKey: requiredEnv("INSFORGE_API_KEY")
  });

  // Sweep completed TO DO items from previous days before reading. Best-effort:
  // a failed sweep must not break the dashboard render. See deleteStaleCompletedTodoItems.
  await deleteStaleCompletedTodoItems(admin);
  // Reset completed DAILY CHORES items from previous days to not-done (daily
  // chores recur, so they reset rather than delete). See resetDailyChoresItems.
  await resetDailyChoresItems(admin);

  const baseStarted = timeMs();
  const [itemsResult, recipesResult] = await Promise.all([
    admin.database
      .from("planner_items")
      .select("id,list_key,text,done,created_at,updated_at")
      .in("list_key", ["todo", "grocery", "daily_chores"])
      .order("created_at", { ascending: false }),
    admin.database
      .from("recipes")
      .select("id,title,photo_url,photo_key,instructions,created_at,updated_at")
      .order("title", { ascending: true })
  ]);
  const baseQueryMs = elapsedMs(baseStarted);

  const { data: items, error: itemsError } = itemsResult;
  if (itemsError) throw itemsError;

  const { data: recipes, error: recipesError } = recipesResult;
  if (recipesError) throw recipesError;

  const recipeRows = recipes as RecipeRow[];
  const recipeIds = recipeRows.map((recipe) => recipe.id);
  let ingredientRows: RecipeIngredientRow[] = [];
  const ingredientsStarted = timeMs();

  if (recipeIds.length > 0) {
    const { data: recipeIngredients, error: ingredientsError } = await admin.database
      .from("recipe_ingredients")
      .select("recipe_id,name,amount,sort_order")
      .in("recipe_id", recipeIds)
      .order("sort_order", { ascending: true });

    if (ingredientsError) throw ingredientsError;
    ingredientRows = recipeIngredients as RecipeIngredientRow[];
  }
  const ingredientsQueryMs = elapsedMs(ingredientsStarted);

  const buildStarted = timeMs();
  const staleCompletedCutoff = Date.now() - COMPLETED_ITEM_HIDE_AFTER_MS;
  const plannerItems = (items as PlannerItem[]).filter((item) => shouldShowPlannerItem(item, staleCompletedCutoff));
  const ingredientsByRecipeId = groupIngredientsByRecipeId(ingredientRows);
  const recipePayloads = recipeRows.map((recipe) => recipePayload(recipe, ingredientsByRecipeId));

  const payloadWithoutVersion = {
    ok: true as const,
    generated_at: `${dashboardLocalDate()}T00:00:00+05:30`,
    lists: (["todo", "grocery", "daily_chores"] as const).map((key) => ({
      key,
      title: LIST_TITLES[key],
      items: plannerItems
        .filter((item) => item.list_key === key)
        .sort((a, b) => {
          if (a.done !== b.done) return a.done ? 1 : -1;
          return b.created_at.localeCompare(a.created_at);
        })
        .map((item) => ({
          id: item.id,
          text: item.text,
          done: item.done,
          updated_at: item.updated_at
        }))
    })),
    recipes: recipePayloads
  };

  const payload = {
    ...payloadWithoutVersion,
    version: hashText(JSON.stringify({
      lists: payloadWithoutVersion.lists,
      recipes: payloadWithoutVersion.recipes
    }))
  };
  logTiming("kindle-dashboard-data", {
    base_query_ms: baseQueryMs,
    ingredients_query_ms: ingredientsQueryMs,
    payload_build_ms: elapsedMs(buildStarted),
    recipe_count: recipeRows.length,
    ingredient_count: ingredientRows.length
  });
  return payload;
}

function groupIngredientsByRecipeId(ingredientRows: RecipeIngredientRow[]): Map<string, RecipeIngredientRow[]> {
  const ingredientsByRecipeId = new Map<string, RecipeIngredientRow[]>();
  for (const ingredient of ingredientRows) {
    const existing = ingredientsByRecipeId.get(ingredient.recipe_id);
    if (existing) existing.push(ingredient);
    else ingredientsByRecipeId.set(ingredient.recipe_id, [ingredient]);
  }
  return ingredientsByRecipeId;
}

function recipePayload(recipe: RecipeRow, ingredientsByRecipeId: Map<string, RecipeIngredientRow[]>): RecipePayload {
  const ingredients = ingredientsByRecipeId.get(recipe.id) ?? [];
  return {
    id: recipe.id,
    title: recipe.title,
    photo_url: recipe.photo_url,
    photo_key: recipe.photo_key,
    instructions: recipe.instructions,
    ingredients: [...ingredients]
      .sort((a, b) => a.sort_order - b.sort_order)
      .map((ingredient) => ({
        name: ingredient.name,
        amount: ingredient.amount,
        sort_order: ingredient.sort_order
      }))
  };
}

function dashboardLocalDate(): string {
  const timezone = Deno.env.get("DASHBOARD_TIMEZONE") || "Asia/Kolkata";
  const parts = new Intl.DateTimeFormat("en-CA", {
    timeZone: timezone,
    year: "numeric",
    month: "2-digit",
    day: "2-digit"
  }).formatToParts(new Date());
  const byType = Object.fromEntries(parts.map((part) => [part.type, part.value]));
  return `${byType.year}-${byType.month}-${byType.day}`;
}

function shouldShowPlannerItem(item: PlannerItem, staleCompletedCutoff: number): boolean {
  if (!item.done) return true;
  const updatedAt = Date.parse(item.updated_at);
  if (!Number.isFinite(updatedAt)) return true;
  return updatedAt > staleCompletedCutoff;
}

// Delete completed TO DO items whose updated_at falls before the start of today
// (in DASHBOARD_TIMEZONE). "Following day" cleanup: the moment the local date
// rolls over, the next fetch sweeps yesterday's completed todos. Grocery and
// not-done items are left alone. Best-effort — callers swallow errors so a
// failed sweep never breaks the dashboard render.
async function deleteStaleCompletedTodoItems(admin: ReturnType<typeof createAdminClient>): Promise<void> {
  try {
    const cutoffIso = new Date(startOfTodayUtcMs()).toISOString();
    const { error } = await admin.database
      .from("planner_items")
      .delete()
      .eq("list_key", "todo")
      .eq("done", true)
      .lt("updated_at", cutoffIso);
    if (error) console.error("deleteStaleCompletedTodoItems:", error.message);
  } catch (error) {
    console.error("deleteStaleCompletedTodoItems:", error instanceof Error ? error.message : String(error));
  }
}

// Reset DAILY CHORES items marked done before the start of today back to
// not-done. Unlike todo (which deletes completed items the next day), daily
// chores recur, so they reset and reappear unchecked each morning. Best-effort
// — a failed reset never breaks the dashboard render. The updated_at trigger
// bumps updated_at on the reset write, which is correct: the item reads as
// "reset today" and stays visible.
async function resetDailyChoresItems(admin: ReturnType<typeof createAdminClient>): Promise<void> {
  try {
    const cutoffIso = new Date(startOfTodayUtcMs()).toISOString();
    const { error } = await admin.database
      .from("planner_items")
      .update({ done: false })
      .eq("list_key", "daily_chores")
      .eq("done", true)
      .lt("updated_at", cutoffIso);
    if (error) console.error("resetDailyChoresItems:", error.message);
  } catch (error) {
    console.error("resetDailyChoresItems:", error instanceof Error ? error.message : String(error));
  }
}

// Epoch milliseconds of midnight in DASHBOARD_TIMEZONE for the current local
// day. Reuses dashboardLocalDate() so the sweep cutoff and the generated_at
// date can never disagree.
function startOfTodayUtcMs(): number {
  const timezone = Deno.env.get("DASHBOARD_TIMEZONE") || "Asia/Kolkata";
  const localDate = dashboardLocalDate(); // YYYY-MM-DD in the project timezone
  // The UTC instant we'd get by reading local midnight as UTC, shifted back by
  // the timezone's offset, yields true local midnight as a UTC instant.
  const midnightAsUtc = Date.parse(`${localDate}T00:00:00Z`);
  return midnightAsUtc - timezoneOffsetMinutes(timezone, Date.now()) * 60000;
}

// Offset (in minutes) of `timezone` from UTC at the given UTC instant, computed
// by formatting the instant in the zone and re-parsing the wall-clock parts as
// if they were UTC. Positive east of UTC (e.g. +330 for Asia/Kolkata).
function timezoneOffsetMinutes(timezone: string, utcMs: number): number {
  const parts = new Intl.DateTimeFormat("en-US", {
    timeZone: timezone,
    hour12: false,
    year: "numeric", month: "2-digit", day: "2-digit",
    hour: "2-digit", minute: "2-digit", second: "2-digit"
  }).formatToParts(new Date(utcMs));
  const byType = Object.fromEntries(parts.map((part) => [part.type, part.value]));
  const wallClockAsUtc = Date.UTC(
    Number(byType.year), Number(byType.month) - 1, Number(byType.day),
    Number(byType.hour), Number(byType.minute), Number(byType.second)
  );
  return Math.round((wallClockAsUtc - utcMs) / 60000);
}

function corsHeaders(): HeadersInit {
  return {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type, X-Dashboard-Read-Token, Authorization"
  };
}

function isAuthorizedDashboardRead(req: Request): boolean {
  const configuredToken = requiredEnv("DASHBOARD_READ_TOKEN");
  const receivedToken =
    req.headers.get("x-dashboard-read-token") ||
    bearerToken(req.headers.get("authorization")) ||
    new URL(req.url).searchParams.get("read_token");
  return Boolean(receivedToken) && receivedToken === configuredToken;
}

function bearerToken(header: string | null): string {
  const match = /^Bearer\s+(.+)$/i.exec(header || "");
  return match?.[1]?.trim() || "";
}

function requiredEnv(key: string): string {
  const value = Deno.env.get(key);
  if (!value) throw new Error(`Missing ${key}`);
  return value;
}

function jsonResponse(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      ...corsHeaders(),
      "Content-Type": "application/json; charset=utf-8",
      "Cache-Control": "no-store"
    }
  });
}

function timeMs(): number {
  return performance.now();
}

function elapsedMs(started: number): number {
  return Math.round(performance.now() - started);
}

function logTiming(label: string, timing: Record<string, number>): void {
  console.log(`${label} timing ${JSON.stringify(timing)}`);
}

function hashText(value: string): string {
  let hash = 2166136261;
  for (let index = 0; index < value.length; index += 1) {
    hash ^= value.charCodeAt(index);
    hash = Math.imul(hash, 16777619);
  }
  return (hash >>> 0).toString(16);
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
