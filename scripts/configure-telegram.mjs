import { execFileSync } from "node:child_process";

const args = new Map();
for (let index = 2; index < process.argv.length; index += 2) {
  args.set(process.argv[index], process.argv[index + 1]);
}

const botToken = args.get("--bot-token") || process.env.TELEGRAM_BOT_TOKEN;
const chatId = args.get("--chat-id") || process.env.TELEGRAM_ALLOWED_CHAT_ID;
const baseUrl = args.get("--base-url") || process.env.INSFORGE_BASE_URL || "";
const webhookUrl =
  args.get("--webhook-url") ||
  process.env.TELEGRAM_WEBHOOK_URL ||
  (baseUrl ? `${baseUrl.replace(/\/+$/, "")}/functions/telegram-webhook` : "");

if (!botToken || !chatId || !webhookUrl) {
  console.error("Usage: npm run telegram:configure -- --bot-token <token> --chat-id <chat-id> --webhook-url <url>");
  process.exit(1);
}

setSecret("TELEGRAM_BOT_TOKEN", botToken);
setSecret("TELEGRAM_ALLOWED_CHAT_ID", chatId);

const webhookSecret = getSecret("TELEGRAM_WEBHOOK_SECRET");
const response = postTelegramWebhook(botToken, webhookUrl, webhookSecret);

if (!response.ok) {
  console.error(JSON.stringify(response, null, 2));
  process.exit(1);
}

// Register the slash-command menu so commands appear in Telegram's / autocomplete.
// Keep this in sync with COMMAND_CATALOG in functions/telegram-webhook.ts.
const commands = [
  { command: "addgrocery", description: "Add item to grocery list" },
  { command: "addtodo", description: "Add item to to-do list" },
  { command: "addchores", description: "Add item to daily chores" },
  { command: "grocery", description: "Show the grocery list" },
  { command: "todo", description: "Show the to-do list" },
  { command: "chores", description: "Show daily chores" },
  { command: "newrecipe", description: "Record a recipe step by step" },
  { command: "recipe", description: "Show a recipe" },
  { command: "editrecipe", description: "Edit a recipe step by step" },
  { command: "deleterecipe", description: "Delete a recipe" },
  { command: "cancel", description: "Cancel a pending add or recipe" },
  { command: "help", description: "Show available commands" }
];
const commandsResponse = postTelegramCommands(botToken, commands);
if (!commandsResponse.ok) {
  console.error(JSON.stringify(commandsResponse, null, 2));
  process.exit(1);
}

console.log(`Telegram webhook registered: ${webhookUrl}`);
console.log(`Telegram command menu registered (${commands.length} commands).`);
console.log(`Allowed chat ID set: ${chatId}`);

function setSecret(key, value) {
  try {
    run(["secrets", "add", key, value]);
  } catch (error) {
    const output = String(error.stdout || "") + String(error.stderr || "") + String(error.message || "");
    if (!output.includes("Secret already exists")) {
      throw error;
    }
    run(["secrets", "update", key, "--value", value]);
  }
}

function getSecret(key) {
  const output = run(["secrets", "get", key, "--json"]);
  return JSON.parse(output).value;
}

function postTelegramWebhook(token, url, secretToken) {
  const body = new URLSearchParams({
    url,
    secret_token: secretToken,
    drop_pending_updates: "true"
  });

  const response = fetchSync(`https://api.telegram.org/bot${token}/setWebhook`, body);
  return JSON.parse(response);
}

function postTelegramCommands(token, commands) {
  const response = execFileSync(
    "curl",
    ["-sS", "--fail-with-body", "-X", "POST",
     `https://api.telegram.org/bot${token}/setMyCommands`,
     "-H", "Content-Type: application/json",
     "--data", JSON.stringify({ commands })],
    { encoding: "utf8" }
  );
  return JSON.parse(response);
}

function fetchSync(url, body) {
  return execFileSync(
    "curl",
    ["-sS", "--fail-with-body", "-X", "POST", url, "-H", "Content-Type: application/x-www-form-urlencoded", "--data", body.toString()],
    { encoding: "utf8" }
  );
}

function run(args) {
  return execFileSync("npx", ["@insforge/cli", ...args], {
    encoding: "utf8",
    stdio: ["ignore", "pipe", "pipe"]
  });
}
