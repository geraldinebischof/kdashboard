import { spawnSync } from "node:child_process";

const DEFAULT_HEALTH_URLS = [
  "https://mkydr6th.eu-central.insforge.app/functions/telegram-webhook",
  "https://mkydr6th.eu-central.insforge.app/functions/kindle-dashboard-data"
].join(",");

const REQUEST_TIMEOUT_MS = 15000;
const RESTORE_POLL_INTERVAL_MS = 30000;
const RESTORE_POLL_TIMEOUT_MS = 10 * 60 * 1000;

const dryRun = process.argv.includes("--dry-run");

function env(name) {
  return (process.env[name] || "").trim();
}

function log(line) {
  console.log(`watchdog: ${line}`);
}

function urlLabel(url) {
  try {
    return new URL(url).pathname;
  } catch {
    return url;
  }
}

const healthUrls = (env("HEALTH_URLS") || DEFAULT_HEALTH_URLS)
  .split(",")
  .map((url) => url.trim())
  .filter(Boolean);
const healthBearerToken = env("HEALTH_BEARER_TOKEN");
const insforgeUserApiKey = env("INSFORGE_USER_API_KEY");
const insforgeProjectId = env("INSORGE_PROJECT_ID");
const telegramBotToken = env("TELEGRAM_BOT_TOKEN");
const telegramAlertChatId = env("TELEGRAM_ALERT_CHAT_ID");

async function checkUrl(url) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
  try {
    const response = await fetch(url, {
      headers: healthBearerToken ? { authorization: `Bearer ${healthBearerToken}` } : {},
      signal: controller.signal
    });
    let body = null;
    try {
      body = await response.json();
    } catch {}
    if (response.status === 200) {
      return body && body.ok === false
        ? { url, ok: false, detail: "status=200 ok=false" }
        : { url, ok: true, detail: "status=200" };
    }
    if ((response.status === 401 || response.status === 403) && !healthBearerToken) {
      return { url, ok: true, detail: `status=${response.status} reachable_auth_required` };
    }
    return { url, ok: false, detail: `status=${response.status}` };
  } catch (error) {
    return { url, ok: false, detail: `error=${error?.name || "network_error"}` };
  } finally {
    clearTimeout(timer);
  }
}

async function checkAll() {
  const results = await Promise.all(healthUrls.map(checkUrl));
  return { results, healthy: results.every((result) => result.ok) };
}

function summarize(check) {
  return check.results.map((result) => `${urlLabel(result.url)}=${result.detail}`).join(" ");
}

async function sendTelegramAlert(text) {
  if (!telegramBotToken || !telegramAlertChatId) {
    log(`telegram_alert_skipped reason=missing_config text="${text}"`);
    return;
  }
  try {
    const response = await fetch(`https://api.telegram.org/bot${telegramBotToken}/sendMessage`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ chat_id: telegramAlertChatId, text })
    });
    log(response.ok ? "telegram_alert_sent" : `telegram_alert_failed status=${response.status}`);
  } catch (error) {
    log(`telegram_alert_failed error=${error?.name || "network_error"}`);
  }
}

function runInsforgeCli(cliArgs) {
  const result = spawnSync("npx", ["@insforge/cli", ...cliArgs], { encoding: "utf8" });
  return {
    exitCode: result.status,
    stdout: (result.stdout || "").trim(),
    stderr: (result.stderr || "").trim()
  };
}

function projectArgs() {
  return insforgeProjectId ? ["--project", insforgeProjectId] : [];
}

function parseJsonOrNull(text) {
  try {
    return JSON.parse(text);
  } catch {
    return null;
  }
}

async function waitForHealthy() {
  const deadline = Date.now() + RESTORE_POLL_TIMEOUT_MS;
  while (Date.now() < deadline) {
    await new Promise((resolve) => setTimeout(resolve, RESTORE_POLL_INTERVAL_MS));
    const check = await checkAll();
    if (check.healthy) return true;
    log(`still_unhealthy ${summarize(check)}`);
  }
  return false;
}

async function main() {
  if (healthUrls.length === 0) {
    console.error("watchdog: no health URLs configured");
    process.exit(1);
  }

  const check = await checkAll();
  log(`${check.healthy ? "healthy" : "unhealthy"} ${summarize(check)}`);
  if (check.healthy) process.exit(0);

  if (dryRun) {
    log("dry_run restore_and_alerts_skipped");
    process.exit(1);
  }

  if (!insforgeUserApiKey) {
    await sendTelegramAlert("deshy watchdog: backend health checks are failing and no INSFORGE_USER_API_KEY is configured, so I cannot auto-restore. Manual fix needed.");
    process.exit(1);
  }

  const login = runInsforgeCli(["login", "--user-api-key", insforgeUserApiKey]);
  if (login.exitCode !== 0) {
    log(`insforge_login_failed exit=${login.exitCode}`);
    await sendTelegramAlert("deshy watchdog: backend health checks are failing and InsForge CLI login failed. Check the INSFORGE_USER_API_KEY secret.");
    process.exit(1);
  }

  const projectGet = runInsforgeCli(["projects", "get", "--json", ...projectArgs()]);
  const project = parseJsonOrNull(projectGet.stdout);
  const status = String(project?.status ?? "");
  if (projectGet.exitCode !== 0 || !status) {
    log(`projects_get_failed exit=${projectGet.exitCode} stderr=${projectGet.stderr.slice(0, 200)}`);
    await sendTelegramAlert("deshy watchdog: backend health checks are failing and the InsForge project status lookup failed. Manual fix needed.");
    process.exit(1);
  }

  if (!status.toLowerCase().includes("paused")) {
    log(`project_status=${status}`);
    await sendTelegramAlert(`deshy watchdog: backend health checks are failing but the project reports status "${status}" (not paused). No auto-restore attempted. Manual fix needed.`);
    process.exit(1);
  }

  log("project_status=paused restore_started");
  const restore = runInsforgeCli(["projects", "restore", "--json", "--yes", ...projectArgs()]);
  log(`restore_exit=${restore.exitCode} ${restore.stdout || restore.stderr}`.trimEnd());

  const restored = await waitForHealthy();
  if (restored) {
    await sendTelegramAlert("deshy watchdog: the backend had auto-paused. I restored it and health checks are green again. The bot should respond now.");
    log("restored_healthy");
    process.exit(0);
  }

  await sendTelegramAlert("deshy watchdog: the backend had auto-paused. Restore ran but health checks still fail after 10 minutes. Manual fix needed.");
  process.exit(1);
}

main();
