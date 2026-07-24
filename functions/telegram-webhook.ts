import { createAdminClient } from "npm:@insforge/sdk";

type ListKey = "grocery" | "todo" | "daily_chores";

type PlannerAction = {
  kind?: "planner";
  action: "add" | "complete" | "uncomplete" | "delete" | "clear" | "list";
  list_key: ListKey;
  items: string[];
  all_lists?: boolean;
};

type RecipeIngredientInput = {
  name: string;
  amount: string;
};

// A full snapshot to upsert (create or wholesale replace). The same action is
// reused by /newrecipe and the edit flow so both write rows identically.
type AddRecipeAction = {
  kind: "recipe";
  action: "add_recipe";
  title: string;
  instructions: string;
  ingredients: RecipeIngredientInput[];
  // Set only by the edit flow to pin the update to the original recipe id, so a
  // title change edits the right row instead of re-matching by title. Absent
  // for normal creates (which match/insert by title as before).
  recipe_id?: string;
};

// Surgical single-message edits against an existing recipe, resolved by title.
type AddIngredientAction = {
  kind: "recipe";
  action: "add_ingredient";
  title: string;
  ingredient: RecipeIngredientInput;
};
type RemoveIngredientAction = {
  kind: "recipe";
  action: "remove_ingredient";
  title: string;
  name: string;
};
type SetInstructionsAction = {
  kind: "recipe";
  action: "set_instructions";
  title: string;
  instructions: string;
};

type RecipeAction =
  | AddRecipeAction
  | AddIngredientAction
  | RemoveIngredientAction
  | SetInstructionsAction
  | { kind: "recipe"; action: "delete_recipe"; title: string }
  | { kind: "recipe"; action: "view_recipe"; title: string };

type TelegramAction = PlannerAction | RecipeAction;

// The full command catalog, shown on /help so every command is one glance away.
// Kept in sync with the Telegram native / menu registered by
// scripts/configure-telegram.mjs.
const COMMAND_CATALOG = [
  "/addgrocery <item> — add to grocery",
  "/addtodo <item> — add to to-do",
  "/addchores <item> — add to daily chores",
  "/grocery — show the grocery list",
  "/todo — show the to-do list",
  "/chores — show daily chores",
  "/newrecipe [title] — record a recipe step by step",
  "/recipe <title> — show a recipe",
  "/editrecipe <title> — edit a recipe step by step",
  "/deleterecipe <title> — delete a recipe",
  "…or just say it: “add 100g butter to Pancakes”, “remove eggs from Pancakes”, “show recipe Pancakes”"
].join("\n");

// The short hint appended after every successful action. Kept to just the add
// commands so replies stay compact — the full list lives in /help.
const REPLY_CATALOG = [
  "/addgrocery <item> — add to grocery",
  "/addtodo <item> — add to to-do",
  "/addchores <item> — add to daily chores"
].join("\n");

function appendCatalog(summary: string): string {
  return `${summary}\n\n${REPLY_CATALOG}`;
}

type TelegramUpdate = {
  message?: {
    chat?: { id?: number | string };
    text?: string;
    photo?: unknown[]; // reserved for future photo capture; ignored in this flow
  };
};

export default async function(req: Request): Promise<Response> {
  const started = timeMs();
  if (req.method === "OPTIONS") {
    return new Response(null, { status: 204, headers: corsHeaders() });
  }

  if (req.method === "GET") {
    return jsonResponse({ ok: true, service: "telegram-webhook" });
  }

  if (req.method !== "POST") {
    return jsonResponse({ ok: false, error: "Method not allowed" }, 405);
  }

  const configuredSecret = requiredEnv("TELEGRAM_WEBHOOK_SECRET");
  const receivedSecret = req.headers.get("x-telegram-bot-api-secret-token");
  if (receivedSecret !== configuredSecret) {
    return jsonResponse({ ok: false, error: "Unauthorized" }, 401);
  }

  const update = (await req.json()) as TelegramUpdate;
  const chatId = String(update.message?.chat?.id ?? "");
  const allowedChatId = requiredEnv("TELEGRAM_ALLOWED_CHAT_ID");
  if (chatId !== allowedChatId) {
    return jsonResponse({ ok: true, ignored: true, reason: "chat_not_allowed" });
  }

  const text = update.message?.text?.trim();
  if (!text) {
    return jsonResponse({ ok: true, ignored: true, reason: "no_text" });
  }

  const admin = createAdminClient({
    baseUrl: requiredEnv("INSFORGE_BASE_URL"),
    apiKey: requiredEnv("INSFORGE_API_KEY")
  });

  // Slash commands are the primary interface. They run before the draft gate
  // (so /newrecipe starts a draft) and before the natural-language parser.
  const commandHandled = await handleSlashCommand(admin, chatId, text);
  if (commandHandled) {
    return jsonResponse({ ok: true, handled: "slash_command" });
  }

  // Pending add flow: a bare /addgrocery (or /addtodo, /addchores) tap stores
  // the intended list and waits for the item. The next plain-text message is
  // routed here and added to that list. Must run before the recipe draft gate
  // and the free-text parser — otherwise "milk" would default to to-do.
  const pendingHandled = await handlePendingAdd(admin, chatId, text);
  if (pendingHandled) {
    return jsonResponse({ ok: true, handled: "pending_add" });
  }

  // Pending recipe command: a bare /recipe (or /editrecipe, /deleterecipe) tap
  // stashes the command and waits for the title. The next plain-text message
  // is the title, routed to that command. Same reason as above: without this,
  // the title would fall through to the free-text parser and land in to-do.
  const pendingRecipeHandled = await handlePendingRecipeCommand(admin, chatId, text);
  if (pendingRecipeHandled) {
    return jsonResponse({ ok: true, handled: "pending_recipe_command" });
  }

  // Chat-form recipe flow takes priority over the one-shot parser. /newrecipe
  // starts a draft; any message while a draft is active is routed to the draft
  // handler. Only when no draft exists does the message reach the normal
  // natural-language parser, so existing behavior is unchanged outside a flow.
  const draftHandled = await handleRecipeDraftFlow(admin, chatId, text);
  if (draftHandled) {
    return jsonResponse({ ok: true, handled: "recipe_draft" });
  }

  const parseStarted = timeMs();
  const action = await parseTelegramMessage(text);
  const parseMs = elapsedMs(parseStarted);
  if (!action) {
    sendTelegramMessageInBackground(chatId, "I could not understand that update.");
    return jsonResponse({ ok: true, ignored: true, reason: "unparsed" });
  }

  const applyStarted = timeMs();
  const summary = await applyTelegramAction(admin, action);
  const applyMs = elapsedMs(applyStarted);
  sendTelegramMessageInBackground(chatId, appendCatalog(summary));
  logTiming("telegram-webhook", {
    action: action.kind || "planner",
    parse_ms: parseMs,
    apply_ms: applyMs,
    total_ms: elapsedMs(started)
  });

  return jsonResponse({ ok: true, action, summary });
}

async function parseTelegramMessage(message: string): Promise<TelegramAction | null> {
  const fastAction = parseFastHeuristicMessage(message);
  if (fastAction) return fastAction;

  const openAiKey = Deno.env.get("OPENAI_API_KEY");
  if (!openAiKey) {
    return parseMessageHeuristically(message);
  }

  const response = await fetch("https://api.openai.com/v1/chat/completions", {
    method: "POST",
    headers: {
      "Authorization": `Bearer ${openAiKey}`,
      "Content-Type": "application/json"
    },
    body: JSON.stringify({
      model: Deno.env.get("OPENAI_MODEL") || "gpt-4o-mini",
      messages: [
        {
          role: "system",
          content:
            [
              "Parse one Telegram dashboard message into strict JSON.",
              "For planner/list updates return: {\"kind\":\"planner\",\"action\":\"add|complete|uncomplete|delete|clear|list\",\"list_key\":\"grocery|todo|daily_chores\",\"items\":[\"short item\"],\"all_lists\":false}. Use list_key \"todo\" for to-do items/tasks and \"daily_chores\" for recurring daily chores. Use [] only for clear. Use action \"list\" when the user wants to view/read/show the items in a list (e.g. \"what's on the grocery list?\", \"show me my todos\").",
              "For recipes return one of these JSON shapes: create/replace whole recipe: {\"kind\":\"recipe\",\"action\":\"add_recipe\",\"title\":\"Recipe Title\",\"instructions\":\"optional steps\",\"ingredients\":[{\"name\":\"Paneer\",\"amount\":\"100 g\"}]}; add one ingredient to an existing recipe: {\"kind\":\"recipe\",\"action\":\"add_ingredient\",\"title\":\"Recipe Title\",\"ingredient\":{\"name\":\"Butter\",\"amount\":\"100 g\"}}; remove ingredient(s): {\"kind\":\"recipe\",\"action\":\"remove_ingredient\",\"title\":\"Recipe Title\",\"name\":\"eggs\"}; replace steps only: {\"kind\":\"recipe\",\"action\":\"set_instructions\",\"title\":\"Recipe Title\",\"instructions\":\"mix and fry\"}; view a recipe: {\"kind\":\"recipe\",\"action\":\"view_recipe\",\"title\":\"Recipe Title\"}; delete a recipe: {\"kind\":\"recipe\",\"action\":\"delete_recipe\",\"title\":\"Recipe Title\"}."
            ].join(" ")
        },
        { role: "user", content: message }
      ],
      response_format: { type: "json_object" },
      temperature: 0
    })
  });

  if (!response.ok) {
    return parseMessageHeuristically(message);
  }

  const payload = await response.json();
  const content = payload?.choices?.[0]?.message?.content;
  if (typeof content !== "string") {
    return parseMessageHeuristically(message);
  }

  try {
    return validateTelegramAction(JSON.parse(content)) ?? parseMessageHeuristically(message);
  } catch {
    return parseMessageHeuristically(message);
  }
}

async function applyTelegramAction(admin: any, action: TelegramAction): Promise<string> {
  if (isRecipeAction(action)) return applyRecipeAction(admin, action);
  return applyPlannerAction(admin, action);
}

async function applyPlannerAction(admin: any, action: PlannerAction): Promise<string> {
  const listName = plannerListLabel(action.list_key);
  if (action.action === "list") {
    // Reused by both the /grocery slash command and the free-text "show me
    // the grocery list" parser so the output stays identical.
    return summarizeList(admin, action.list_key);
  }

  if (action.action === "clear") {
    const { error } = await admin.database
      .from("planner_items")
      .delete()
      .eq("list_key", action.list_key);
    if (error) throw error;
    return `Cleared ${listName}.`;
  }

  if (action.action === "add") {
    const rows = action.items.map((text) => ({ list_key: action.list_key, text, done: false }));
    const { error } = await admin.database.from("planner_items").insert(rows);
    if (error) throw error;
    return `Added ${action.items.join(", ")} to ${listName}.`;
  }

  const done = action.action === "complete";
  if (action.action === "complete" || action.action === "uncomplete") {
    for (const item of action.items) {
      let query = admin.database
        .from("planner_items")
        .update({ done })
        .ilike("text", `%${item}%`);
      if (!action.all_lists) {
        query = query.eq("list_key", action.list_key);
      }
      const { error } = await query;
      if (error) throw error;
    }
    return `${done ? "Marked done" : "Marked open"}: ${action.items.join(", ")}.`;
  }

  for (const item of action.items) {
    let query = admin.database
      .from("planner_items")
      .delete()
      .ilike("text", `%${item}%`);
    if (!action.all_lists) {
      query = query.eq("list_key", action.list_key);
    }
    const { error } = await query;
    if (error) throw error;
  }

  return `Removed ${action.items.join(", ")} from ${listName}.`;
}

function plannerListLabel(listKey: ListKey): string {
  if (listKey === "todo") return "to-do";
  if (listKey === "daily_chores") return "daily chores";
  return listKey;
}

// Reads a planner list and formats it for Telegram, showing only items still
// open (not done) — the bot is used to see what's left, so completed items are
// omitted. Newest first, bullet-pointed. Kept as plain text (no parse_mode) to
// match the rest of this webhook — user item text is uncontrolled, so Markdown
// would need escaping.
async function summarizeList(admin: any, listKey: ListKey): Promise<string> {
  const { data, error } = await admin.database
    .from("planner_items")
    .select("text,created_at")
    .eq("list_key", listKey)
    .eq("done", false)
    .order("created_at", { ascending: false });
  if (error) throw error;

  const rows = Array.isArray(data) ? data : [];
  const label = plannerListLabel(listKey);
  if (rows.length === 0) {
    return `${label[0].toUpperCase()}${label.slice(1)} list is empty.`;
  }

  const lines = rows.map((row: { text: string }) => `• ${row.text}`);
  const header = `${label} (${rows.length} open)`;
  return `${header}\n${lines.join("\n")}`;
}

async function applyRecipeAction(admin: any, action: RecipeAction): Promise<string> {
  switch (action.action) {
    case "add_recipe":
      return applyAddRecipe(admin, action);
    case "add_ingredient":
      return applyAddIngredient(admin, action);
    case "remove_ingredient":
      return applyRemoveIngredient(admin, action);
    case "set_instructions":
      return applySetInstructions(admin, action);
    case "view_recipe":
      return applyViewRecipe(admin, action);
    case "delete_recipe":
      return applyDeleteRecipe(admin, action);
  }
}

// Free-text "show recipe X". Same renderer as /recipe, reached via the parser.
async function applyViewRecipe(admin: any, action: { title: string }): Promise<string> {
  const recipe = await findRecipeByTitle(admin, action.title);
  if (!recipe) return recipeNotFound(action.title);
  return summarizeRecipe(recipe);
}

// Free-text "delete recipe X". Cascades to ingredients via FK.
async function applyDeleteRecipe(admin: any, action: { title: string }): Promise<string> {
  const recipe = await findRecipeByTitle(admin, action.title);
  if (!recipe) return recipeNotFound(action.title);
  const { error } = await admin.database.from("recipes").delete().eq("id", recipe.id);
  if (error) throw error;
  return `Deleted recipe: ${recipe.title}.`;
}

// Upserts a full recipe snapshot. Matches by title unless `recipe_id` is set
// (the edit flow pins the update to the original id so a title change edits
// the right row). Ingredients are replaced wholesale. photo_url/photo_key are
// never written here, so create and edit both preserve any existing photo.
async function applyAddRecipe(admin: any, action: AddRecipeAction): Promise<string> {
  const recipeRow = {
    title: action.title,
    instructions: action.instructions || ""
  };

  let recipeId = action.recipe_id || "";

  if (recipeId) {
    // Edit flow: update the pinned row regardless of title.
    const { error } = await admin.database
      .from("recipes")
      .update(recipeRow)
      .eq("id", recipeId);
    if (error) throw error;
  } else {
    const { data: existing, error: selectError } = await admin.database
      .from("recipes")
      .select("id")
      .eq("title", action.title)
      .limit(1);
    if (selectError) throw selectError;

    if (Array.isArray(existing) && existing.length > 0) {
      recipeId = String(existing[0].id);
      const { error } = await admin.database
        .from("recipes")
        .update(recipeRow)
        .eq("id", recipeId);
      if (error) throw error;
    } else {
      const { data, error } = await admin.database
        .from("recipes")
        .insert([recipeRow])
        .select("id");
      if (error) throw error;
      recipeId = String(Array.isArray(data) ? data[0]?.id ?? "" : data?.id ?? "");
    }
  }

  if (!recipeId) throw new Error("Recipe insert did not return an id");

  const { error: deleteError } = await admin.database
    .from("recipe_ingredients")
    .delete()
    .eq("recipe_id", recipeId);
  if (deleteError) throw deleteError;

  if (action.ingredients.length > 0) {
    const rows = action.ingredients.map((ingredient, index) => ({
      recipe_id: recipeId,
      name: ingredient.name,
      amount: ingredient.amount,
      sort_order: index + 1
    }));
    const { error } = await admin.database.from("recipe_ingredients").insert(rows);
    if (error) throw error;
  }

  const ingredientCount = action.ingredients.length;
  return `Saved recipe: ${action.title}${ingredientCount > 0 ? ` (${ingredientCount} ingredient${ingredientCount === 1 ? "" : "s"})` : ""}.`;
}

// Targeted tweak: append one ingredient at the end of the recipe's sort order.
async function applyAddIngredient(admin: any, action: AddIngredientAction): Promise<string> {
  const recipe = await findRecipeByTitle(admin, action.title);
  if (!recipe) return recipeNotFound(action.title);

  const nextSort = recipe.ingredients.reduce((max, ing) => Math.max(max, ing.sort_order), 0) + 1;
  const { error } = await admin.database.from("recipe_ingredients").insert([{
    recipe_id: recipe.id,
    name: action.ingredient.name,
    amount: action.ingredient.amount,
    sort_order: nextSort
  }]);
  if (error) throw error;

  return `Added ${action.ingredient.amount} ${action.ingredient.name} to ${recipe.title}.`;
}

// Targeted tweak: remove ingredient(s) by name match within the recipe only.
async function applyRemoveIngredient(admin: any, action: RemoveIngredientAction): Promise<string> {
  const recipe = await findRecipeByTitle(admin, action.title);
  if (!recipe) return recipeNotFound(action.title);

  const { data, error } = await admin.database
    .from("recipe_ingredients")
    .delete()
    .eq("recipe_id", recipe.id)
    .ilike("name", `%${action.name}%`)
    .select("name");
  if (error) throw error;

  const removed = Array.isArray(data) ? data.length : 0;
  if (removed === 0) {
    return `No "${action.name}" in ${recipe.title}.`;
  }
  return `Removed ${removed} ingredient${removed === 1 ? "" : "s"} from ${recipe.title}.`;
}

// Targeted tweak: replace only the instructions, leave title + ingredients.
async function applySetInstructions(admin: any, action: SetInstructionsAction): Promise<string> {
  const recipe = await findRecipeByTitle(admin, action.title);
  if (!recipe) return recipeNotFound(action.title);

  const { error } = await admin.database
    .from("recipes")
    .update({ instructions: action.instructions })
    .eq("id", recipe.id);
  if (error) throw error;

  return `Updated steps for ${recipe.title}.`;
}

// Loads a recipe by title (case-insensitive, exact). Titles are UNIQUE so at
// most one match. Returns the recipe with its ingredients sorted.
async function findRecipeByTitle(admin: any, title: string): Promise<{
  id: string;
  title: string;
  instructions: string;
  ingredients: { name: string; amount: string; sort_order: number }[];
} | null> {
  const { data, error } = await admin.database
    .from("recipes")
    .select("id,title,instructions")
    .ilike("title", title)
    .limit(1);
  if (error) throw error;
  const row = Array.isArray(data) && data.length > 0 ? data[0] : null;
  if (!row) return null;

  const { data: ingData, error: ingError } = await admin.database
    .from("recipe_ingredients")
    .select("name,amount,sort_order")
    .eq("recipe_id", row.id)
    .order("sort_order", { ascending: true });
  if (ingError) throw ingError;

  return {
    id: String(row.id),
    title: String(row.title),
    instructions: String(row.instructions || ""),
    ingredients: Array.isArray(ingData) ? ingData.map((i: any) => ({
      name: String(i.name),
      amount: String(i.amount),
      sort_order: Number(i.sort_order)
    })) : []
  };
}

function recipeNotFound(title: string): string {
  return `No recipe called "${title}".`;
}

// Renders a recipe for Telegram. Shared by /recipe and the edit-flow intro so
// every view of a recipe looks identical. Plain text (no parse_mode) — recipe
// text is uncontrolled, so Markdown would need escaping.
function summarizeRecipe(recipe: {
  title: string;
  instructions: string;
  ingredients: { name: string; amount: string; sort_order: number }[];
}): string {
  const lines = recipe.ingredients.map((ing) => `• ${ing.amount} ${ing.name}`);
  const parts = [recipe.title];
  if (lines.length > 0) parts.push(lines.join("\n"));
  if (recipe.instructions) parts.push(`Steps: ${recipe.instructions}`);
  return parts.join("\n");
}

// ─── Slash command dispatcher ──────────────────────────────────────────────
// Primary command interface. Runs before the draft gate and the natural-language
// parser. Returns true if the message was a recognized command (and the caller
// should short-circuit). Free-text messages fall through unchanged.
const ADD_COMMANDS: Record<string, ListKey> = {
  addgrocery: "grocery",
  addtodo: "todo",
  addchores: "daily_chores"
};

const SHOW_COMMANDS: Record<string, ListKey> = {
  grocery: "grocery",
  todo: "todo",
  chores: "daily_chores"
};

async function handleSlashCommand(admin: any, chatId: string, text: string): Promise<boolean> {
  const match = text.match(/^\/(\w+)\b\s*(.*)$/i);
  if (!match) return false;
  const command = match[1].toLowerCase();
  const args = match[2].trim();

  if (command === "help") {
    sendTelegramMessageInBackground(chatId, COMMAND_CATALOG);
    return true;
  }

  if (command === "cancel") {
    // Clears any pending add. (Recipe drafts are cancelled by their own flow
    // below; /cancel here is a convenience for the add flow.)
    await clearPendingAdd(admin, chatId);
    sendTelegramMessageInBackground(chatId, appendCatalog("Cancelled."));
    return true;
  }

  if (command === "newrecipe") {
    // Start the chat-form draft directly; handleRecipeDraftFlow below only
    // advances an existing draft, it does not start one.
    await startRecipeDraft(admin, chatId, args);
    return true;
  }

  if (command === "recipe") {
    if (!args) {
      // Telegram's / menu auto-sends on tap, so a bare /recipe stashes the
      // command and waits for the title — same tap-then-type pattern as
      // /addgrocery. Without this, the next message (the title) would fall
      // through to the free-text parser.
      await setPendingRecipeCommand(admin, chatId, "recipe");
      sendTelegramMessageInBackground(chatId, "Which recipe? e.g. Pancakes (or type \"cancel\")");
      return true;
    }
    await replyViewRecipe(admin, chatId, args);
    return true;
  }

  if (command === "editrecipe") {
    if (!args) {
      await setPendingRecipeCommand(admin, chatId, "editrecipe");
      sendTelegramMessageInBackground(chatId, "Which recipe should I edit? e.g. Pancakes (or type \"cancel\")");
      return true;
    }
    await startEditRecipeDraft(admin, chatId, args);
    return true;
  }

  if (command === "deleterecipe") {
    if (!args) {
      await setPendingRecipeCommand(admin, chatId, "deleterecipe");
      sendTelegramMessageInBackground(chatId, "Which recipe should I delete? e.g. Pancakes (or type \"cancel\")");
      return true;
    }
    await replyDeleteRecipe(admin, chatId, args);
    return true;
  }


  const showListKey = SHOW_COMMANDS[command];
  if (showListKey) {
    const summary = await summarizeList(admin, showListKey);
    sendTelegramMessageInBackground(chatId, appendCatalog(summary));
    return true;
  }

  const listKey = ADD_COMMANDS[command];
  if (listKey) {
    const items = args
      .split(/\s*(?:,|;|\+| and )\s*/i)
      .map((item) => item.trim())
      .filter(Boolean);
    if (items.length === 0) {
      // Telegram's / menu auto-sends on tap, so a bare /addgrocery is the
      // expected entry. Stash the intended list and ask for the item; the next
      // plain-text message is routed to this list by handlePendingAdd.
      await setPendingAdd(admin, chatId, listKey);
      sendTelegramMessageInBackground(chatId, `What should I add to ${plannerListLabel(listKey)}? (comma-separate multiples, or type "cancel")`);
      return true;
    }
    await clearPendingAdd(admin, chatId);
    const summary = await applyPlannerAction(admin, {
      kind: "planner",
      action: "add",
      list_key: listKey,
      items,
      all_lists: false
    });
    sendTelegramMessageInBackground(chatId, appendCatalog(summary));
    return true;
  }

  // Unknown slash command: acknowledge so the user isn't left guessing whether
  // their message was received. Don't fall through to free-text for commands.
  sendTelegramMessageInBackground(chatId, appendCatalog(`I don't know /${command}.`));
  return true;
}

// ─── Pending add flow (bare /addgrocery, /addtodo, /addchores) ──────────────
// After a bare add-command tap, the intended list is stashed in pending_adds.
// The next plain-text message is parsed as one or more items and added to that
// list. "cancel" (handled by the dispatcher) clears the pending state.
async function handlePendingAdd(admin: any, chatId: string, text: string): Promise<boolean> {
  const pending = await getPendingAdd(admin, chatId);
  // A recipe-command row (command set, list_key null) is handled by
  // handlePendingRecipeCommand below — don't claim it as a list add.
  if (!pending || !pending.list_key) return false;

  const lower = text.toLowerCase().trim();
  if (lower === "cancel" || lower === "/cancel" || lower === "nevermind" || lower === "never mind") {
    await clearPendingAdd(admin, chatId);
    sendTelegramMessageInBackground(chatId, appendCatalog("Cancelled."));
    return true;
  }

  const items = text
    .split(/\s*(?:,|;|\+| and )\s*/i)
    .map((item) => item.trim())
    .filter(Boolean);
  if (items.length === 0) {
    sendTelegramMessageInBackground(chatId, `What should I add to ${plannerListLabel(pending.list_key as ListKey)}? (comma-separate multiples, or type "cancel")`);
    return true;
  }

  await clearPendingAdd(admin, chatId);
  const summary = await applyPlannerAction(admin, {
    kind: "planner",
    action: "add",
    list_key: pending.list_key as ListKey,
    items,
    all_lists: false
  });
  sendTelegramMessageInBackground(chatId, appendCatalog(summary));
  return true;
}

async function getPendingAdd(admin: any, chatId: string): Promise<{ list_key: string } | null> {
  const { data, error } = await admin.database
    .from("pending_adds")
    .select("list_key")
    .eq("chat_id", chatId)
    .limit(1);
  if (error) throw error;
  const row = Array.isArray(data) && data.length > 0 ? data[0] : null;
  // Preserve a genuine null list_key (a pending recipe-command row) rather than
  // coercing it to the string "null" — which is truthy and would slip past the
  // caller's null check. Rows without a list_key belong to handlePendingRecipeCommand.
  if (!row || row.list_key === null || row.list_key === undefined) return null;
  return { list_key: String(row.list_key) };
}

async function setPendingAdd(admin: any, chatId: string, listKey: ListKey): Promise<void> {
  const { error } = await admin.database
    .from("pending_adds")
    .upsert({ chat_id: chatId, list_key: listKey }, { onConflict: "chat_id" });
  if (error) throw error;
}

async function clearPendingAdd(admin: any, chatId: string): Promise<void> {
  const { error } = await admin.database
    .from("pending_adds")
    .delete()
    .eq("chat_id", chatId);
  if (error) throw error;
}

// ─── Pending recipe command flow (bare /recipe, /editrecipe, /deleterecipe) ─
// Same tap-then-type pattern as the pending add flow. Telegram's / menu
// auto-sends a bare command on tap, so the intended command is stashed in
// pending_adds (with `command` set, list_key null). The next plain-text
// message is the recipe title and is routed to the matching command. Reuses
// the pending_adds table (one row per chat) so it naturally replaces any
// pending list-add and vice versa.
async function handlePendingRecipeCommand(admin: any, chatId: string, text: string): Promise<boolean> {
  const command = await getPendingRecipeCommand(admin, chatId);
  if (!command) return false;

  const lower = text.toLowerCase().trim();
  if (lower === "cancel" || lower === "/cancel" || lower === "nevermind" || lower === "never mind") {
    await clearPendingAdd(admin, chatId);
    sendTelegramMessageInBackground(chatId, appendCatalog("Cancelled."));
    return true;
  }

  const title = text.trim();
  if (!title) return false; // let it fall through normally

  await clearPendingAdd(admin, chatId);
  if (command === "recipe") {
    await replyViewRecipe(admin, chatId, title);
  } else if (command === "editrecipe") {
    await startEditRecipeDraft(admin, chatId, title);
  } else if (command === "deleterecipe") {
    await replyDeleteRecipe(admin, chatId, title);
  }
  return true;
}

async function getPendingRecipeCommand(admin: any, chatId: string): Promise<string | null> {
  const { data, error } = await admin.database
    .from("pending_adds")
    .select("command")
    .eq("chat_id", chatId)
    .limit(1);
  if (error) throw error;
  const row = Array.isArray(data) && data.length > 0 ? data[0] : null;
  if (!row || row.command === null || row.command === undefined) return null;
  return String(row.command);
}

async function setPendingRecipeCommand(admin: any, chatId: string, command: string): Promise<void> {
  const { error } = await admin.database
    .from("pending_adds")
    .upsert({ chat_id: chatId, command, list_key: null }, { onConflict: "chat_id" });
  if (error) throw error;
}

// Resolves a recipe by title and replies with it. Shared by /recipe (with and
// without args) and the pending recipe-command flow.
async function replyViewRecipe(admin: any, chatId: string, title: string): Promise<void> {
  const recipe = await findRecipeByTitle(admin, title);
  if (!recipe) {
    sendTelegramMessageInBackground(chatId, recipeNotFound(title));
    return;
  }
  sendTelegramMessageInBackground(chatId, summarizeRecipe(recipe));
}

// Resolves a recipe by title and deletes it (ingredients cascade). Shared by
// /deleterecipe (with and without args) and the pending recipe-command flow.
async function replyDeleteRecipe(admin: any, chatId: string, title: string): Promise<void> {
  const recipe = await findRecipeByTitle(admin, title);
  if (!recipe) {
    sendTelegramMessageInBackground(chatId, recipeNotFound(title));
    return;
  }
  const { error } = await admin.database.from("recipes").delete().eq("id", recipe.id);
  if (error) throw error;
  sendTelegramMessageInBackground(chatId, `Deleted recipe: ${recipe.title}.`);
}

// ─── Chat-form recipe draft flow (/newrecipe) ──────────────────────────────
// Persists an in-progress recipe in recipe_drafts so the stateless webhook can
// collect title → ingredients → instructions across many messages. The final
// save delegates to applyRecipeAction so drafts and one-shot parses write the
// same rows the same way.

type RecipeDraft = {
  chat_id: string;
  title: string;
  ingredients: RecipeIngredientInput[];
  instructions: string;
  stage: "title" | "ingredients" | "instructions";
  // 'create' is the default /newrecipe flow. 'edit' is the /editrecipe flow,
  // which pre-seeds existing fields and pins the final save to recipe_id.
  mode: "create" | "edit";
  recipe_id: string; // empty for create; the pinned recipe id for edit
};

// Returns true if this message was part of a recipe draft flow (and the caller
// should return early without invoking the normal parser). Returns false when
// no draft is active and the user isn't starting one.
async function handleRecipeDraftFlow(admin: any, chatId: string, text: string): Promise<boolean> {
  // /newrecipe is handled by handleSlashCommand before this runs; here we only
  // advance an existing draft through its title/ingredients/instructions stages.
  const draft = await getActiveDraft(admin, chatId);
  if (!draft) return false;

  const lower = text.toLowerCase();
  if (lower === "cancel" || lower === "/cancel") {
    await cancelDraft(admin, chatId);
    return true;
  }

  if (draft.stage === "title") {
    const title = text.trim();
    if (!title) {
      sendTelegramMessageInBackground(chatId, "The title can't be empty. Send one, or type `cancel`.");
      return true;
    }
    await updateDraftStage(admin, chatId, { title, stage: "ingredients" });
    sendTelegramMessageInBackground(
      chatId,
      `Got it — ${title}. Send each ingredient on its own message (e.g. "200g flour"). Type "done" when you've added all ingredients, or "cancel".`
    );
    return true;
  }

  if (draft.stage === "ingredients") {
    const isEdit = draft.mode === "edit";
    // Exit the ingredients stage. In create mode, "skip" jumps with none and
    // "done" finalizes whatever's been added. In edit mode, "keep"/"done"
    // preserve the existing (pre-seeded) ingredients.
    if (lower === "keep" || lower === "done" || lower === "skip" || text.trim() === "") {
      await updateDraftStage(admin, chatId, { stage: "instructions" });
      const count = draft.ingredients.length;
      sendTelegramMessageInBackground(
        chatId,
        isEdit
          ? `Keeping ${count} ingredient${count === 1 ? "" : "s"}. Send the steps for ${draft.title || "this recipe"} as one message, or "keep" to leave them unchanged.`
          : (count === 0 && lower === "skip")
            ? `No problem — send the steps for ${draft.title || "this recipe"} as one message, then it'll be saved. Or type "cancel".`
            : `Got ${count} ingredient${count === 1 ? "" : "s"}. Send the steps for ${draft.title || "this recipe"} as one message, and it'll be saved. Or type "cancel".`
      );
      return true;
    }
    const parsed = parseIngredientLine(text);
    if (!parsed) {
      sendTelegramMessageInBackground(chatId, `That didn't look like an ingredient (e.g. "200g flour"). Try again, or type "done" or "cancel".`);
      return true;
    }
    const next = [...draft.ingredients, parsed];
    await updateDraftStage(admin, chatId, { ingredients: next });
    sendTelegramMessageInBackground(
      chatId,
      `Added ${parsed.amount} ${parsed.name} (${next.length} so far). Send the next, or type "done".`
    );
    return true;
  }

  if (draft.stage === "instructions") {
    // In edit mode, "keep" preserves the existing instructions (pre-seeded into
    // the draft). In create mode, empty instructions are allowed.
    const instructions = lower === "keep" && draft.mode === "edit"
      ? draft.instructions
      : text.trim();
    await finalizeDraft(admin, chatId, { instructions });
    return true;
  }

  // Unknown stage: clear the draft so the user isn't stuck.
  await cancelDraft(admin, chatId);
  sendTelegramMessageInBackground(chatId, "Cleared an unfinished recipe draft.");
  return true;
}

async function getActiveDraft(admin: any, chatId: string): Promise<RecipeDraft | null> {
  const { data, error } = await admin.database
    .from("recipe_drafts")
    .select("chat_id,title,ingredients,instructions,stage,mode,recipe_id")
    .eq("chat_id", chatId)
    .limit(1);
  if (error) throw error;
  const row = Array.isArray(data) && data.length > 0 ? data[0] : null;
  if (!row) return null;
  const ingredients = Array.isArray(row.ingredients) ? row.ingredients : [];
  const stage = (["title", "ingredients", "instructions"].includes(String(row.stage)) ? row.stage : "title") as RecipeDraft["stage"];
  const mode = (String(row.mode) === "edit" ? "edit" : "create") as RecipeDraft["mode"];
  return {
    chat_id: chatId,
    title: String(row.title || ""),
    ingredients,
    instructions: String(row.instructions || ""),
    stage,
    mode,
    recipe_id: row.recipe_id ? String(row.recipe_id) : ""
  };
}

async function startRecipeDraft(admin: any, chatId: string, title: string): Promise<void> {
  if (title) {
    await upsertDraft(admin, chatId, { title, ingredients: [], instructions: "", stage: "ingredients", mode: "create", recipe_id: "" });
    sendTelegramMessageInBackground(
      chatId,
      `Starting recipe: ${title}. Send each ingredient on its own message (e.g. "200g flour"). Type "done" when you've added all ingredients, or "cancel".`
    );
  } else {
    await upsertDraft(admin, chatId, { title: "", ingredients: [], instructions: "", stage: "title", mode: "create", recipe_id: "" });
    sendTelegramMessageInBackground(chatId, `Let's record a recipe. What's the title? (Or type "cancel".)`);
  }
}

// Starts an edit draft for an existing recipe. Pre-seeds the current title,
// ingredients, and instructions so each stage can "keep" them. Pins the final
// save to the recipe id so a title change edits the right row.
async function startEditRecipeDraft(admin: any, chatId: string, title: string): Promise<void> {
  const recipe = await findRecipeByTitle(admin, title);
  if (!recipe) {
    sendTelegramMessageInBackground(chatId, recipeNotFound(title));
    return;
  }
  await upsertDraft(admin, chatId, {
    title: recipe.title,
    ingredients: recipe.ingredients.map((i) => ({ name: i.name, amount: i.amount })),
    instructions: recipe.instructions,
    stage: "ingredients",
    mode: "edit",
    recipe_id: recipe.id
  });
  sendTelegramMessageInBackground(
    chatId,
    `Editing:\n${summarizeRecipe(recipe)}\n\nTitle is "${recipe.title}". Send new ingredients one per message (they'll be added to the current ones), "keep"/"done" to keep as-is, or "cancel".`
  );
}

async function upsertDraft(admin: any, chatId: string, fields: { title: string; ingredients: RecipeIngredientInput[]; instructions: string; stage: RecipeDraft["stage"]; mode: RecipeDraft["mode"]; recipe_id: string }): Promise<void> {
  const row = { chat_id: chatId, title: fields.title, ingredients: fields.ingredients, instructions: fields.instructions, stage: fields.stage, mode: fields.mode, recipe_id: fields.recipe_id || null };
  const { error } = await admin.database
    .from("recipe_drafts")
    .upsert(row, { onConflict: "chat_id" });
  if (error) throw error;
}

async function updateDraftStage(admin: any, chatId: string, patch: Partial<Pick<RecipeDraft, "title" | "ingredients" | "stage">>): Promise<void> {
  const { error } = await admin.database
    .from("recipe_drafts")
    .update(patch)
    .eq("chat_id", chatId);
  if (error) throw error;
}

async function finalizeDraft(admin: any, chatId: string, patch: { instructions: string }): Promise<void> {
  const draft = await getActiveDraft(admin, chatId);
  if (!draft) {
    sendTelegramMessageInBackground(chatId, "There's no recipe draft in progress. Send /newrecipe to start one.");
    return;
  }
  const action: AddRecipeAction = {
    kind: "recipe",
    action: "add_recipe",
    title: draft.title,
    instructions: patch.instructions,
    ingredients: draft.ingredients,
    // Pin the save to the original recipe in edit mode so a title change still
    // updates the right row.
    recipe_id: draft.mode === "edit" ? draft.recipe_id : undefined
  };
  try {
    const summary = await applyRecipeAction(admin, action);
    await deleteDraft(admin, chatId);
    sendTelegramMessageInBackground(chatId, appendCatalog(summary));
  } catch (error) {
    // Don't drop the draft on save failure — the user can retry the final step.
    sendTelegramMessageInBackground(chatId, `Couldn't save recipe: ${errorMessage(error)}. Try sending the steps again, or type "cancel".`);
  }
}

async function cancelDraft(admin: any, chatId: string): Promise<void> {
  await deleteDraft(admin, chatId);
  sendTelegramMessageInBackground(chatId, "Recipe cancelled.");
}

async function deleteDraft(admin: any, chatId: string): Promise<void> {
  const { error } = await admin.database
    .from("recipe_drafts")
    .delete()
    .eq("chat_id", chatId);
  if (error) throw error;
}

async function sendTelegramMessage(chatId: string, text: string): Promise<void> {
  const token = Deno.env.get("TELEGRAM_BOT_TOKEN");
  if (!token) return;

  await fetch(`https://api.telegram.org/bot${token}/sendMessage`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ chat_id: chatId, text })
  });
}

function sendTelegramMessageInBackground(chatId: string, text: string): void {
  sendTelegramMessage(chatId, text).catch((error) => {
    console.error(`telegram-webhook reply_error ${errorMessage(error)}`);
  });
}

function corsHeaders(): HeadersInit {
  return {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type, X-Telegram-Bot-Api-Secret-Token"
  };
}

function parseFastHeuristicMessage(message: string): TelegramAction | null {
  const normalized = message.trim().replace(/\s+/g, " ");
  const lower = normalized.toLowerCase();

  // "show me the grocery list", "what's on my todo list?", "read chores" — a
  // read request is detected before the planner-verb gate so it isn't grabbed
  // by "get"/"need" etc. Only fires when a list is named; bare "show list" is
  // left to fall through.
  const listRequest = parseListRequest(normalized, lower);
  if (listRequest) return listRequest;

  // Recipe view/delete/tweaks use distinctive structural shapes. Checked
  // before the planner-verb gate so "add 100g butter to Pancakes" isn't grabbed
  // as a planner add, and before the recipe-create heuristic.
  const recipeEdit = parseRecipeTargetedEdit(normalized);
  if (recipeEdit) return recipeEdit;
  const recipeView = parseRecipeViewRequest(normalized);
  if (recipeView) return recipeView;

  const hasPlannerVerb = /\b(add|put|include|buy|get|need|mark|check off|complete|completed|done|undo|uncheck|delete|remove|drop|clear|empty|reset)\b/.test(lower);
  if (hasPlannerVerb && hasExplicitList(normalized)) {
    return parseMessageHeuristically(normalized);
  }

  return null;
}

// Detects a read-the-list intent (e.g. "show me the grocery list", "what's on
// my to-do?", "read my chores"). Requires an explicit list name so a bare
// "show list" doesn't silently match anything. Verbs are restricted to
// unambiguous read words: "get" and "list" are intentionally excluded because
// they collide with write intent ("get milk", "add eggs to grocery list").
const LIST_REQUEST_VERBS =
  /\b(show|see|view|read|display|what(?:'s| is|s)|tell me|current)\b/i;

function parseListRequest(normalized: string, lower: string): TelegramAction | null {
  if (!LIST_REQUEST_VERBS.test(lower)) return null;
  if (!hasExplicitList(normalized)) return null;
  const listKey = detectListKey(normalized);
  return {
    kind: "planner",
    action: "list",
    list_key: listKey,
    items: [],
    all_lists: false
  };
}

// ─── Recipe free-text detectors ────────────────────────────────────────────
// Targeted tweaks + view/delete are parsed before the recipe *create* heuristic
// (parseRecipeHeuristically) so their distinctive shapes are claimed first.
// Each requires the word "recipe"/"meal" OR a "to/from <Title>" structure so
// planner messages ("add milk to grocery") are never grabbed. Titles are taken
// as the trailing phrase after the structural keyword.

// "add 100g butter to Pancakes", "add 2 eggs to the Pancakes recipe"
const ADD_INGREDIENT_RE = /^\s*(?:please\s+)?add\s+(.+?)\s+(?:to|into)\s+(?:the\s+)?(.+?)(?:\s+recipe)?\s*$/i;
// "remove eggs from Pancakes", "drop sugar from the Pancakes recipe"
const REMOVE_INGREDIENT_RE = /^\s*(?:please\s+)?(?:remove|drop|delete)\s+(.+?)\s+(?:from|in)\s+(?:the\s+)?(.+?)(?:\s+recipe)?\s*$/i;
// "set Pancakes steps to: mix and fry", "update Pancakes instructions to: ..."
const SET_INSTRUCTIONS_RE = /^\s*(?:please\s+)?(?:set|update|change)\s+(.+?)\s+(?:steps|instructions|directions)\s+to\s*:?\s*(.+?)\s*$/i;
// "show recipe Pancakes", "view the Pancakes recipe", "what's in the pancake recipe"
const VIEW_RECIPE_RE = /\b(recipe|meal)\b/i;

function parseRecipeTargetedEdit(normalized: string): TelegramAction | null {
  const lower = normalized.toLowerCase();

  // set <title> steps to: ...
  const setMatch = normalized.match(SET_INSTRUCTIONS_RE);
  if (setMatch) {
    const instructions = setMatch[2].trim();
    if (!instructions) return null;
    return { kind: "recipe", action: "set_instructions", title: setMatch[1].trim(), instructions };
  }

  // add <ingredient> to <title>
  const addMatch = normalized.match(ADD_INGREDIENT_RE);
  if (addMatch) {
    const ingredient = parseIngredientLine(addMatch[1].trim());
    if (!ingredient) return null;
    // Yield to the planner when the destination is a planner list ("add milk to
    // grocery"), not a recipe title. Without this guard the planner add is
    // stolen and reported as "No recipe called grocery".
    if (hasExplicitList(addMatch[2])) return null;
    return { kind: "recipe", action: "add_ingredient", title: addMatch[2].trim(), ingredient };
  }

  // remove <name> from <title>
  const removeMatch = normalized.match(REMOVE_INGREDIENT_RE);
  if (removeMatch) {
    const name = removeMatch[1].trim();
    if (!name) return null;
    // Same guard as above: "remove eggs from todo" is a planner delete, not a
    // recipe ingredient removal.
    if (hasExplicitList(removeMatch[2])) return null;
    return { kind: "recipe", action: "remove_ingredient", title: removeMatch[2].trim(), name };
  }

  // Guard the tweaks below: only treat "add/remove X to/from Y" as a recipe
  // edit when the word recipe/meal is present OR the structural regex already
  // matched above. (The regexes above already bound this, so this lower bound
  // is a second safety net for ambiguous bare phrases.)

  // delete recipe X
  if (/\b(delete|remove|drop)\b/.test(lower) && VIEW_RECIPE_RE.test(lower)) {
    const title = normalized
      .replace(/^(?:please\s+)?(?:delete|remove|drop)\s+(?:the\s+)?(?:recipe|meal)\s*/i, "")
      .replace(/\b(?:recipe|meal)\b/gi, "")
      .trim();
    if (title) return { kind: "recipe", action: "delete_recipe", title };
  }

  return null;
}

function parseRecipeViewRequest(normalized: string): TelegramAction | null {
  const lower = normalized.toLowerCase();
  if (!VIEW_RECIPE_RE.test(lower)) return null;
  if (!LIST_REQUEST_VERBS.test(lower)) return null;
  const title = normalized
    .replace(/^(?:please\s+)?(?:show|see|view|read|display|tell me(?:\s+(?:about|in))?)\s+(?:me\s+)?(?:the\s+)?/i, "")
    .replace(/^(?:what(?:'s| is)?\s+(?:in|on))\s+(?:the\s+)?/i, "")
    .replace(/\b(?:recipe|meal)\b/gi, "")
    .replace(/[?.!]+$/g, "")
    .trim();
  if (!title) return null;
  return { kind: "recipe", action: "view_recipe", title };
}

function jsonResponse(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { ...corsHeaders(), "Content-Type": "application/json; charset=utf-8" }
  });
}

function requiredEnv(key: string): string {
  const value = Deno.env.get(key);
  if (!value) throw new Error(`Missing ${key}`);
  return value;
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function timeMs(): number {
  return Date.now();
}

function elapsedMs(started: number): number {
  return Date.now() - started;
}

function logTiming(label: string, timing: Record<string, number | string>): void {
  console.log(`${label} timing ${JSON.stringify(timing)}`);
}

const LIST_ALIASES: Record<ListKey, string[]> = {
  grocery: ["grocery", "groceries", "shopping", "market"],
  todo: ["todo", "to-do", "task", "tasks", "errand", "errands"],
  daily_chores: ["daily chore", "daily chores", "chores", "chore", "daily routine", "routine"]
};

const LIST_KEYS: ListKey[] = ["grocery", "todo", "daily_chores"];

function detectListKey(message: string): ListKey {
  const lower = message.toLowerCase();
  for (const key of LIST_KEYS) {
    if (LIST_ALIASES[key].some((alias) => lower.includes(alias))) return key;
  }
  return "todo";
}

function parseMessageHeuristically(message: string): TelegramAction {
  const normalized = message.trim().replace(/\s+/g, " ");
  const lower = normalized.toLowerCase();

  // Recipe targeted edits and views first — their structural shapes must be
  // claimed before the recipe-create heuristic below, otherwise "add 100g
  // butter to Pancakes" would be misparsed as a create.
  const recipeEdit = parseRecipeTargetedEdit(normalized);
  if (recipeEdit) return recipeEdit;
  const recipeView = parseRecipeViewRequest(normalized);
  if (recipeView) return recipeView;

  const recipeAction = parseRecipeHeuristically(message);
  if (recipeAction) return recipeAction;

  const listRequest = parseListRequest(normalized, lower);
  if (listRequest) return listRequest;

  const listKey = detectListKey(normalized);
  const explicitList = hasExplicitList(normalized);

  let action: PlannerAction["action"] = "add";
  if (/\b(undo|uncheck|not done|incomplete)\b/.test(lower)) {
    action = "uncomplete";
  } else if (/\b(delete|remove|drop)\b/.test(lower)) {
    action = "delete";
  } else if (/\b(clear|empty|reset)\b/.test(lower)) {
    action = "clear";
  } else if (/\b(done|complete|completed|check off|mark)\b/.test(lower)) {
    action = "complete";
  }

  const withoutActionFirst = normalized
    .replace(/^(please\s+)?(add|put|include|buy|get|need|mark|check off|complete|completed|done|undo|uncheck|delete|remove|drop|clear|empty|reset)\s+/i, "")
    .replace(/\s+(done|complete|completed)$/i, "")
    .replace(/\s+(to|in|on|from)\s+(my\s+)?(grocery|groceries|shopping|market|todo|to-do|task|tasks|errand|errands|daily chore|daily chores|chores|chore|daily routine|routine)(\s+list|\s+plan)?$/i, "")
    .replace(/\s+(to|in|on|from)$/i, "")
    .trim();
  const withoutAction = stripListWords(withoutActionFirst, listKey)
    .replace(/\s+(to|in|on|from)$/i, "")
    .trim();

  const items =
    action === "clear"
      ? []
      : withoutAction
          .split(/\s*(?:,| and |\+)\s*/i)
          .map((item) => item.replace(/^the\s+/i, "").trim())
          .filter(Boolean);

  return {
    kind: "planner",
    action,
    list_key: listKey,
    items: items.length > 0 ? items : [withoutAction || normalized],
    all_lists: !explicitList && (action === "complete" || action === "uncomplete" || action === "delete")
  };
}

function validateTelegramAction(input: unknown): TelegramAction | null {
  return validateRecipeAction(input) ?? validatePlannerAction(input);
}

function validatePlannerAction(input: unknown): PlannerAction | null {
  if (!input || typeof input !== "object") return null;
  const candidate = input as Partial<PlannerAction>;

  if (!["add", "complete", "uncomplete", "delete", "clear", "list"].includes(String(candidate.action))) return null;
  if (!LIST_KEYS.includes(candidate.list_key as ListKey)) return null;

  const items = Array.isArray(candidate.items)
    ? candidate.items.map((item) => String(item).trim()).filter(Boolean)
    : [];

  if (candidate.action !== "clear" && candidate.action !== "list" && items.length === 0) return null;

  return {
    kind: "planner",
    action: candidate.action as PlannerAction["action"],
    list_key: candidate.list_key as ListKey,
    items,
    all_lists: Boolean(candidate.all_lists)
  };
}

function validateRecipeAction(input: unknown): RecipeAction | null {
  if (!input || typeof input !== "object") return null;
  const candidate = input as Record<string, unknown>;
  if (candidate.kind !== "recipe") return null;
  const action = typeof candidate.action === "string" ? candidate.action : "";
  const title = typeof candidate.title === "string" ? candidate.title.trim() : "";
  if (!title) return null;

  if (action === "add_recipe") {
    const ingredients = Array.isArray(candidate.ingredients)
      ? candidate.ingredients
          .map((ingredient) => normalizeRecipeIngredient(ingredient))
          .filter((ingredient): ingredient is RecipeIngredientInput => ingredient !== null)
      : [];
    return {
      kind: "recipe",
      action: "add_recipe",
      title,
      instructions: typeof candidate.instructions === "string" ? candidate.instructions.trim() : "",
      ingredients
    };
  }

  if (action === "add_ingredient") {
    const ingredient = normalizeRecipeIngredient(candidate.ingredient);
    if (!ingredient) return null;
    return { kind: "recipe", action: "add_ingredient", title, ingredient };
  }

  if (action === "remove_ingredient") {
    const name = typeof candidate.name === "string" ? candidate.name.trim() : "";
    if (!name) return null;
    return { kind: "recipe", action: "remove_ingredient", title, name };
  }

  if (action === "set_instructions") {
    const instructions = typeof candidate.instructions === "string" ? candidate.instructions.trim() : "";
    if (!instructions) return null;
    return { kind: "recipe", action: "set_instructions", title, instructions };
  }

  if (action === "view_recipe") {
    return { kind: "recipe", action: "view_recipe", title };
  }

  if (action === "delete_recipe") {
    return { kind: "recipe", action: "delete_recipe", title };
  }

  return null;
}

function normalizeRecipeIngredient(input: unknown): RecipeIngredientInput | null {
  if (!input || typeof input !== "object") return null;
  const candidate = input as Partial<RecipeIngredientInput>;
  const name = typeof candidate.name === "string" ? candidate.name.trim() : "";
  const amount = typeof candidate.amount === "string" ? candidate.amount.trim() : "";
  if (!name || !amount) return null;
  return { name, amount };
}

function parseRecipeHeuristically(message: string): RecipeAction | null {
  const normalized = message.trim().replace(/\s+/g, " ");
  const lower = normalized.toLowerCase();
  if (!/\b(recipe|meal)\b/.test(lower) || !/\b(add|save|create)\b/.test(lower)) return null;

  const title = extractRecipeTitle(normalized);
  if (!title) return null;

  return {
    kind: "recipe",
    action: "add_recipe",
    title,
    instructions: extractAfterLabel(normalized, "instructions") || extractAfterLabel(normalized, "directions") || "",
    ingredients: parseIngredientsList(extractAfterLabel(normalized, "ingredients"))
  };
}

function extractRecipeTitle(message: string): string {
  return message
    .replace(/^(please\s+)?(add|save|create)\s+(a\s+)?(new\s+)?(recipe|meal)\s*/i, "")
    .split(/\b(?:ingredients|instructions|directions)\b/i)[0]
    .replace(/[:,-]\s*$/g, "")
    .trim();
}

function extractAfterLabel(message: string, label: string): string {
  const pattern = new RegExp(`\\b${label}\\b\\s*:?\\s*(.+)$`, "i");
  const match = message.match(pattern);
  if (!match) return "";
  return match[1]
    .split(/\b(?:instructions|directions)\b\s*:?/i)[0]
    .trim();
}

function parseIngredientsList(raw: string): RecipeIngredientInput[] {
  if (!raw) return [];
  return raw
    .split(/\s*(?:,|;| and )\s*/i)
    .map((item) => item.trim())
    .filter(Boolean)
    .map(parseIngredientLine)
    .filter((ingredient): ingredient is RecipeIngredientInput => ingredient !== null);
}

// Parse a single ingredient line (e.g. "200g flour") into {name, amount}.
// Shared by the one-shot recipe parser and the chat-form draft flow so both
// split ingredients the same way. Returns null on empty input.
// Parse a single ingredient line into {name, amount}. Handles the common
// real-world forms: a leading quantity with optional unit, either attached
// ("200ml milk", "2tsp sugar") or separated ("2 eggs", "1 cup flour"). When no
// leading quantity is present ("a pinch of salt"), the whole line becomes the
// name with amount "1 serving".
function parseIngredientLine(item: string): RecipeIngredientInput | null {
  const trimmed = item.trim();
  if (!trimmed) return null;
  const match = trimmed.match(
    /^([0-9][0-9./]*\s*(?:ml|l|liter|liters|litre|litres|g|kg|kgs|gram|grams|tbsp|tablespoon|tablespoons|tsp|teaspoon|teaspoons|cup|cups|medium|piece|pieces|cloves?|slices?|sprigs?|cans?|pkts?|packs?)?)\s+(.*)$/i
  );
  let amount: string;
  let name: string;
  if (match && match[1]) {
    amount = match[1].trim();
    name = match[2].trim();
  } else {
    amount = "1 serving";
    name = trimmed;
  }
  if (!name) return null;
  return { name, amount };
}

function isRecipeAction(action: TelegramAction): action is RecipeAction {
  return (action as RecipeAction).kind === "recipe";
}

function stripListWords(message: string, listKey: ListKey): string {
  let output = message;
  for (const alias of LIST_ALIASES[listKey]) {
    output = output.replace(new RegExp(`\\b${escapeRegExp(alias)}\\b`, "ig"), "");
  }
  return output.replace(/\s+(list|plan)\b/gi, " ").replace(/\s+/g, " ").trim();
}

function escapeRegExp(value: string): string {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function hasExplicitList(message: string): boolean {
  const lower = message.toLowerCase();
  return LIST_KEYS.some((key) => LIST_ALIASES[key].some((alias) => lower.includes(alias)));
}
