# Kindle Dashboard Bring-Your-Own-Backend Install

This guide is for Kindle owners who want their own private dashboard instance.
You will run your own InsForge backend, Telegram bot, and Kindle KUAL package.

If you want a coding assistant to walk through this with you, start with
`docs/SETUP_WITH_ASSISTANT.md`.

## What You Need

- A jailbroken Kindle with KUAL installed.
- A Mac/Linux machine with Node.js 20+ and npm.
- An InsForge account.
- A Telegram bot token from BotFather.
- Optional: Zig or an ARM Kindle cross compiler if you want to rebuild the native binary yourself.

The Kindle package never stores the InsForge admin API key. It reads from public
dashboard endpoints and sends item toggles through the deployed toggle function.

## 1. Create Your Backend

Clone the repo, install dependencies, and log in to InsForge:

```sh
npm install
npx @insforge/cli login
```

Create a fresh InsForge project, or link this checkout to an existing empty one:

```sh
npx @insforge/cli create --name kindle-dashboard --region us-east --template empty
```

Bootstrap the schema, generated secrets, and functions:

```sh
npm run kit:backend
```

The bootstrap script applies the public schema migrations, creates generated
`TELEGRAM_WEBHOOK_SECRET`, `DASHBOARD_READ_TOKEN`, and
`DASHBOARD_TOGGLE_TOKEN` values if missing, and deploys the dashboard functions.

It skips optional sample recipe/photo migrations by default. To include the
sample-data migrations anyway:

```sh
npm run kit:backend -- --with-sample-data
```

## 2. Add Required Backend Secrets

Set the backend URL and API key for the functions. These are server-side
function secrets, not Kindle-side values.

```sh
npx @insforge/cli secrets add INSFORGE_BASE_URL https://your-project.insforge.app
npx @insforge/cli secrets add INSFORGE_API_KEY your-server-only-api-key
```

Optional natural-language parsing:

```sh
npx @insforge/cli secrets add OPENAI_API_KEY your-openai-key
npx @insforge/cli secrets add OPENAI_MODEL gpt-4o-mini
```

If `OPENAI_API_KEY` is missing, the Telegram webhook uses its built-in command
parser.

## 3. Connect Telegram

Create a bot with BotFather and send it one message. Then discover your chat ID:

```sh
npm run telegram:chat-id -- --bot-token 123456789:telegram-bot-token
```

Register the webhook and store your bot token/chat allowlist:

```sh
npm run telegram:configure -- \
  --bot-token 123456789:telegram-bot-token \
  --chat-id 123456789 \
  --webhook-url https://your-project.insforge.app/functions/telegram-webhook
```

Try a Telegram command:

```text
/addgrocery            (then type: milk and eggs)
/addtodo               (then type: clean desk)
/grocery               (shows the grocery list)
/newrecipe Pancakes
```

The `/` autocomplete menu (registered by `telegram:configure`) lists the
available commands — tap one and the bot prompts you for the rest. The catalog
also appears in chat after every successful action, so you always have it one
glance away.

## Supported Telegram Messages

The bot has two interfaces: **slash commands** (primary) and **free text**
(fallback). If `OPENAI_API_KEY` is configured, the free-text fallback
understands more natural phrasing; if not, the built-in heuristic parser
handles the patterns below.

### Slash Commands

Tap a command in Telegram's `/` menu, then type the item when prompted:

```text
/addgrocery             add to grocery (tap, then type item)
/addtodo                add to to-do
/addchores              add to daily chores
/grocery                show the grocery list
/todo                   show the to-do list
/chores                 show daily chores
/newrecipe [title]      record a recipe step by step
/recipe <title>         show a recipe
/editrecipe <title>     edit a recipe step by step
/deleterecipe <title>   delete a recipe
/cancel                 cancel a pending add or recipe
/help                   show the command catalog
```

You can also include the item in the same message (`/addgrocery milk, eggs`),
but the tap-then-type pattern is the quick way — you never have to type the
command name.

Example session:

```text
You: /addgrocery
Bot: What should I add to grocery? (comma-separate multiples, or type "cancel")
You: milk, eggs, flour
Bot: Added milk, eggs, flour to grocery.
```

### Free-Text Fallback

Anything that isn't a slash command is parsed as natural language. Use this for
done/delete/clear actions (no slash command for those).

### Planner Lists

Supported lists:

- Grocery: `grocery`, `groceries`, `shopping`, `market`
- To-do: `todo`, `to-do`, `task`, `tasks`, `errand`, `errands`
- Daily chores: `daily chore`, `daily chores`, `chores`, `chore`, `daily routine`, `routine`

Add items:

```text
add milk and eggs to groceries
need apples, yogurt, oats in grocery
add clean desk to todo
put laundry on daily chores
```

View a list (also available as `/grocery`, `/todo`, `/chores`):

```text
show me the grocery list
what's on my todo?
read my chores
```

Mark items done:

```text
mark milk done
check off clean desk from todo
```

Mark items open again:

```text
undo milk
mark clean desk not done
```

Remove items:

```text
remove eggs from groceries
drop clean desk from todo
```

Clear a list:

```text
clear todo
empty groceries
```

If a done/open/remove command does not name a list, the webhook searches across
lists for matching item text.

### Saved Recipes

Create or update a saved recipe in one message. Ingredients and instructions
are optional.

```text
add recipe Sample Wrap ingredients tortilla 1 piece, beans 100 g instructions roll and toast
save meal Sample Smoothie ingredients milk 200 ml, fruit 0.5 cup
create recipe Sample Bowl ingredients yogurt 200 g, oats 20 g
```

For longer recipes, use the chat-form flow to record a recipe step by step.
Send `/newrecipe [title]` (or bare `/newrecipe` to be prompted), then answer
the bot's prompts:

```text
/newrecipe Pancakes
2 eggs
200ml milk
150g flour
done
Mix dry into wet, rest 10 min, fry in a hot pan.
```

Each ingredient is its own message. Type `done` to finish ingredients, send the
steps as one final message, and the recipe is saved. Type `cancel` at any step
to drop the draft.

### Recipe Editing & Deletion

View a recipe to see its current ingredients and steps:

```text
/recipe Pancakes
show recipe Pancakes
what's in the pancake recipe?
```

Edit a whole recipe step by step. `/editrecipe` loads the existing recipe and
walks you through ingredients then steps; type `keep` (or `done`) to preserve a
field as-is, or send a new value to replace it. New ingredients are added to
the current ones:

```text
/editrecipe Pancakes
keep                       (keep the current ingredients)
100g butter                (add one more)
done
keep                       (keep the existing steps)
```

For quick tweaks, edit a recipe in one message without touching the rest:

```text
add 100g butter to Pancakes        (append an ingredient)
remove eggs from Pancakes          (remove an ingredient by name)
set Pancakes steps to: mix and fry (replace just the steps)
```

Delete a recipe entirely (its ingredients are removed with it):

```text
/deleterecipe Pancakes
delete recipe Pancakes
```

## 4. Configure the Kindle Package

Build or download `kindle-dashboard-kual.tar.gz`.

Run the local syntax/render check before packaging:

```sh
npm run native:check
```

If you have Zig installed, build a soft-float ARM KUAL package:

```sh
make -C kindle/native extension-zig ZIG=/path/to/zig
```

If you have a dedicated Kindle ARM compiler, build with:

```sh
make -C kindle/native extension
```

The GNU build expects `arm-linux-gnueabi-g++` by default. Override with
`KINDLE_CXX=/path/to/compiler` if your toolchain uses a different binary name.

The package is written to:

```text
kindle/native/build/kindle-dashboard-kual.tar.gz
```

Copy it to the Kindle and extract it into the KUAL extensions directory:

```sh
tar -C /mnt/us/extensions -xzf kindle-dashboard-kual.tar.gz
```

Copy the example config and edit the URLs:

```sh
cd /mnt/us/extensions/kindle-dashboard
cp config.sh.example config.sh
```

`config.sh` should look like this:

```sh
DASHBOARD_DATA_URL="https://your-project.insforge.app/functions/kindle-dashboard-data"
DASHBOARD_EVENTS_URL="https://your-project.function2.insforge.app/kindle-dashboard-events"
DASHBOARD_TOGGLE_URL="https://your-project.insforge.app/functions/kindle-dashboard-toggle"
DASHBOARD_READ_TOKEN="replace-with-your-generated-read-token"
DASHBOARD_TOGGLE_TOKEN="replace-with-your-generated-toggle-token"
INTERVAL="3600"
DASHBOARD_KEEP_AWAKE="1"
DASHBOARD_SLEEP_WINDOW="off"
INVERT_IMAGES="0"
```

Fetch the tokens from InsForge and paste them into `config.sh`:

```sh
npx @insforge/cli secrets get DASHBOARD_READ_TOKEN --json
npx @insforge/cli secrets get DASHBOARD_TOGGLE_TOKEN --json
```

Use the direct `function2.insforge.app` host for the events URL. InsForge's
regular `/functions/...` gateway can buffer SSE responses.

If you use the local installer script, set your data URL explicitly so it does
not seed from the wrong backend:

```sh
DASHBOARD_DATA_URL=https://your-project.insforge.app/functions/kindle-dashboard-data DASHBOARD_READ_TOKEN=<read-token> npm run native:install
```

## 5. Launch On Kindle

On the Kindle, open KUAL:

- `Kindle Dashboard -> Refresh Once (Light)` fetches and renders one update for Kindle light mode.
- `Kindle Dashboard -> Refresh Once (Dark)` fetches and renders one update for Kindle dark mode.
- `Kindle Dashboard -> Start Dashboard (Light)` starts hourly refresh for Kindle light mode.
- `Kindle Dashboard -> Start Dashboard (Dark)` starts hourly refresh for Kindle dark mode.
- `Kindle Dashboard -> Stop Dashboard` stops the process.

Useful Kindle-side files:

```text
/mnt/us/documents/kindle-dashboard-native.log
/mnt/us/documents/kindle-dashboard-diagnose.log
/mnt/us/documents/kindle-dashboard-data.json
```

The native app uses an always-on profile: hourly auto-refresh, manual KUAL
refresh on demand, and no overnight quiet mode by default. For optional
auto-open notes, see `kindle/README.md`.

## Updating Later

When you pull a new version:

```sh
npm install
npm run kit:backend
make -C kindle/native extension-zig ZIG=/path/to/zig
```

Then replace the installed KUAL extension files, keeping your local `config.sh`.

## Privacy Notes

- Do not share `INSFORGE_API_KEY`, Telegram bot token, or webhook secret.
- Treat `DASHBOARD_READ_TOKEN` and `DASHBOARD_TOGGLE_TOKEN` as device secrets.
  The read token exposes dashboard data, while the toggle token can change
  checklist state.
- The Kindle reads dashboard data through your deployed function URLs using
  the read token.
- This kit is single-owner by design. For a hosted multi-user service, every
  table and function would need per-user scoping and device pairing.
