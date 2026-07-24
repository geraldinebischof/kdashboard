import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { randomBytes } from "node:crypto";

const schemaMigrations = [
  "migrations/001_planner_lists.sql",
  "migrations/20260629052000_create-recipes.sql",
  "migrations/20260629070000_add-recipe-photo-pgm.sql",
  "migrations/20260707000000_enable_rls_private_tables.sql",
  "migrations/20260721000000_add_daily_chores_list.sql",
  "migrations/20260721120000_drop-recipe-nutrition-and-rating.sql",
  "migrations/20260722120000_create-recipe-drafts.sql",
  "migrations/20260723120000_drop-workout-challenge-health-mealplan.sql",
  "migrations/20260724000000_create-pending-adds.sql",
  "migrations/20260724130000_add-recipe-draft-mode.sql",
  "migrations/20260724140000_pending-adds-recipe-commands.sql"
];

const sampleDataMigrations = [
  "migrations/20260629065000_add-sample-toast.sql",
  "migrations/20260629070100_clear-sample-toast-storage-photo.sql"
];

const functions = [
  ["kindle-dashboard-data", "functions/kindle-dashboard-data.ts", "Kindle Dashboard Data"],
  ["kindle-dashboard-events", "functions/kindle-dashboard-events.ts", "Kindle Dashboard Events"],
  ["kindle-dashboard-toggle", "functions/kindle-dashboard-toggle.ts", "Kindle Dashboard Toggle"],
  ["telegram-webhook", "functions/telegram-webhook.ts", "Telegram Planner Webhook"]
];

const flags = new Set(process.argv.slice(2));
const includeSampleData = flags.has("--with-sample-data");
const skipSecrets = flags.has("--skip-secrets");
const skipDeploy = flags.has("--skip-deploy");

if (flags.has("--help")) {
  console.log(`Usage: npm run kit:backend -- [--with-sample-data] [--skip-secrets] [--skip-deploy]

Applies the public Kindle Dashboard schema to the currently linked InsForge
project, creates missing generated secrets, and deploys edge functions.

Before running:
  npx @insforge/cli login
  npx @insforge/cli create --name kindle-dashboard --region us-east --template empty
`);
  process.exit(0);
}

console.log("Checking linked InsForge project...");
run(["current"]);

console.log("Applying schema migrations...");
for (const migration of schemaMigrations) {
  applyMigration(migration);
}

if (includeSampleData) {
  console.log("Applying optional sample-data migrations...");
  for (const migration of sampleDataMigrations) {
    applyMigration(migration);
  }
}

if (!skipSecrets) {
  console.log("Ensuring generated backend secrets exist...");
  ensureSecret("TELEGRAM_WEBHOOK_SECRET", randomSecret());
  ensureSecret("DASHBOARD_READ_TOKEN", randomSecret());
  ensureSecret("DASHBOARD_TOGGLE_TOKEN", randomSecret());
}

if (!skipDeploy) {
  console.log("Deploying InsForge functions...");
  for (const [slug, file, name] of functions) {
    run(["functions", "deploy", slug, "--file", file, "--name", name]);
  }
}

console.log("Backend kit bootstrap complete.");
console.log("Next: set Telegram secrets with npm run telegram:configure, then copy the endpoints into kindle-dashboard/config.sh.");

function applyMigration(path) {
  console.log(`- ${path}`);
  run(["db", "query", readFileSync(path, "utf8")]);
}

function ensureSecret(key, value) {
  try {
    run(["secrets", "get", key, "--json"], { silent: true });
    console.log(`- ${key} already exists`);
  } catch {
    run(["secrets", "add", key, value]);
    console.log(`- added ${key}`);
  }
}

function randomSecret() {
  return randomBytes(32).toString("hex");
}

function run(args, options = {}) {
  const output = execFileSync("npx", ["@insforge/cli", ...args], {
    encoding: "utf8",
    stdio: options.silent ? ["ignore", "pipe", "pipe"] : "inherit"
  });
  return output || "";
}
