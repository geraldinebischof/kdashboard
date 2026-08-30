// Downloads 24 public-domain vintage Christmas postcards from Wikimedia
// Commons and converts them to dithered monochrome PGMs for the Kindle
// dashboard's advent calendar popup.
//
// Prerequisites: Node 18+ (global fetch) and ImageMagick 7 (`magick`).
// Run from the repo root: node scripts/fetch-advent-images.mjs
//
// Re-runnable: an entry whose target PGM already exists is skipped (warns),
// so the script only fills in missing days.

import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import path from "node:path";

const assetsDir = path.resolve("kindle/kual/kindle-dashboard/assets/advent");

// Curated from Wikimedia Commons (Category:Christmas postcards and themed
// searches). Curator rules: only files explicitly tagged Public domain on
// Commons (verified via the API extmetadata LicenseShortName field); motifs
// restricted to animals, decorations, and plants (no Santa, people, angels,
// or nativity scenes); one motif per day.
const DAYS = [
  { day: 1, motif: "Two deer", file: "File:A. M. Mailick, Christmas postcard with two deer.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/7/78/A._M._Mailick%2C_Christmas_postcard_with_two_deer.jpg" },
  { day: 2, motif: "Deer and rabbit", file: "File:A. M. Mailick, Christmas postcard with deer and rabbit.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/4/45/A._M._Mailick%2C_Christmas_postcard_with_deer_and_rabbit.jpg" },
  { day: 3, motif: "Two deer near a stream", file: "File:Sunrise in the Glen. (9569) (NBY 417920).jpg", url: "https://upload.wikimedia.org/wikipedia/commons/3/33/Sunrise_in_the_Glen._%289569%29_%28NBY_417920%29.jpg" },
  { day: 4, motif: "Deer in winter scene", file: "File:The Head of the Herd. (9915) (NBY 417940).jpg", url: "https://upload.wikimedia.org/wikipedia/commons/6/6e/The_Head_of_the_Herd._%289915%29_%28NBY_417940%29.jpg" },
  { day: 5, motif: "Stag, or red deer", file: "File:The Stag, or Red Deer LCCN2007681452.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/4/4e/The_Stag%2C_or_Red_Deer_LCCN2007681452.jpg" },
  { day: 6, motif: "Two kittens (1915)", file: "File:Eugenie M. Valter - Two Cats - Bevelled Picture Card - Christmas 1915 - obverse.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/0/00/Eugenie_M._Valter_-_Two_Cats_-_Bevelled_Picture_Card_-_Christmas_1915_-_obverse.jpg" },
  { day: 7, motif: "Ducks gathering near trees", file: "File:Untitled (winter landscape, ducks gathering near trees). 232B (NBY 421195).jpg", url: "https://upload.wikimedia.org/wikipedia/commons/4/45/Untitled_%28winter_landscape%2C_ducks_gathering_near_trees%29._232B_%28NBY_421195%29.jpg" },
  { day: 8, motif: "Bird perched on holly branch", file: "File:A happy Christmas. (NBY 418054).jpg", url: "https://upload.wikimedia.org/wikipedia/commons/e/ea/A_happy_Christmas._%28NBY_418054%29.jpg" },
  { day: 9, motif: "Bird flying above holly branch", file: "File:With best Christmas Wishes. (NBY 419074).jpg", url: "https://upload.wikimedia.org/wikipedia/commons/2/21/With_best_Christmas_Wishes._%28NBY_419074%29.jpg" },
  { day: 10, motif: "Two robins on holly branch", file: "File:With best Christmas Wishes. (NBY 419158).jpg", url: "https://upload.wikimedia.org/wikipedia/commons/e/e7/With_best_Christmas_Wishes._%28NBY_419158%29.jpg" },
  { day: 11, motif: "Holly leaves and berries", file: "File:Christmas holly leaves and berries postcard, circa 1900s.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/2/29/Christmas_holly_leaves_and_berries_postcard%2C_circa_1900s.jpg" },
  { day: 12, motif: "A spray of holly", file: "File:Christmas Greetings - A Spray of Holly (Postcard) - DPLA - f6e92648f299023cc8124a0cb92c0a65.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/8/84/Christmas_Greetings_-_A_Spray_of_Holly_%28Postcard%29_-_DPLA_-_f6e92648f299023cc8124a0cb92c0a65.jpg" },
  { day: 13, motif: "Mistletoe", file: "File:Mistletoe Postcard 1900.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/c/cf/Mistletoe_Postcard_1900.jpg" },
  { day: 14, motif: "Pine cones (1905)", file: "File:AK - Fröhliche Weihnachten - Tannenzapfen - 1905.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/8/82/AK_-_Fr%C3%B6hliche_Weihnachten_-_Tannenzapfen_-_1905.jpg" },
  { day: 15, motif: "Fir branch", file: "File:AK - Fröhliche Weihnachten - Tannenzweig - Adresse Wien.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/7/7f/AK_-_Fr%C3%B6hliche_Weihnachten_-_Tannenzweig_-_Adresse_Wien.jpg" },
  { day: 16, motif: "Floral postcard", file: "File:Floral Christmas Postcard.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/6/6e/Floral_Christmas_Postcard.jpg" },
  { day: 17, motif: "Floral postcard (second)", file: "File:Floral Christmas postcard.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/8/87/Floral_Christmas_postcard.jpg" },
  { day: 18, motif: "Victorian primroses", file: "File:Victorian Primroses Christmas card.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/3/3d/Victorian_Primroses_Christmas_card.jpg" },
  { day: 19, motif: "Saxifraga oppositifolia (von Cramm)", file: "File:A Happy Christmas to you. Saxifraga oppositifolia. Sion. By Helga von Cramm. Marcus Ward & Co. c. 1880.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/2/2a/A_Happy_Christmas_to_you._Saxifraga_oppositifolia._Sion._By_Helga_von_Cramm._Marcus_Ward_%26_Co._c._1880.jpg" },
  { day: 20, motif: "Aster alpinus (von Cramm)", file: "File:Christmas card by Helga von Cramm. Aster Alpinus, Usnea Barbata and Lake of Geneva. Chromolithograph.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/6/68/Christmas_card_by_Helga_von_Cramm._Aster_Alpinus%2C_Usnea_Barbata_and_Lake_of_Geneva._Chromolithograph.jpg" },
  { day: 21, motif: "Near Oberhofen, Lake Thun (von Cramm)", file: "File:Helga von Cramm, chromolithograph, Near Oberhofen, on Lake Thun, Happy Christmas card.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/3/30/Helga_von_Cramm%2C_chromolithograph%2C_Near_Oberhofen%2C_on_Lake_Thun%2C_Happy_Christmas_card.jpg" },
  { day: 22, motif: "Snowy forest scene", file: "File:Christmas postcard - Snowy forest scene - Wirth's Brothers.jpg", url: "https://upload.wikimedia.org/wikipedia/commons/c/c1/Christmas_postcard_-_Snowy_forest_scene_-_Wirth%27s_Brothers.jpg" },
  { day: 23, motif: "Christmas still life", file: "File:Untitled (Christmas Still Life.) 284 (NBY 420727).jpg", url: "https://upload.wikimedia.org/wikipedia/commons/2/28/Untitled_%28Christmas_Still_Life.%29_284_%28NBY_420727%29.jpg" },
  { day: 24, motif: "Three bells with holly", file: "File:May Your Christmas Be Happy. (Three bells with holly).jpg", url: "https://upload.wikimedia.org/wikipedia/commons/4/4b/May_Your_Christmas_Be_Happy._%28Three_bells_with_holly%29.jpg" }
];

if (DAYS.length !== 24) {
  console.error(`Curated list has ${DAYS.length} entries, expected 24.`);
  process.exit(1);
}

try {
  execFileSync("magick", ["-version"], { stdio: "ignore" });
} catch {
  console.error("Missing prerequisite: ImageMagick 7 (`magick`) was not found on PATH.");
  console.error("Install it first, for example: brew install imagemagick");
  process.exit(1);
}

mkdirSync(assetsDir, { recursive: true });
const tmpDir = path.join(assetsDir, ".tmp");
mkdirSync(tmpDir, { recursive: true });

const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// Wikimedia rate-limits burst downloads (HTTP 429, up to a 10-minute block).
// Fetch with a politeness delay between downloads; a long Retry-After aborts
// the whole run (it is re-runnable and resumes at the first missing day).
class RateLimitedError extends Error {}

async function downloadWithRetry(url, attempts = 3) {
  for (let attempt = 1; attempt <= attempts; attempt++) {
    try {
      const response = await fetch(url, {
        headers: { "User-Agent": "kdashboard-advent-curation/1.0 (bundled local assets)" }
      });
      if (response.status === 429) {
        const retryAfter = Number(response.headers.get("retry-after")) || 0;
        if (retryAfter > 120) {
          throw new RateLimitedError(`HTTP 429 (blocked for ${retryAfter}s)`);
        }
        if (attempt < attempts) {
          await delay(Math.max(retryAfter * 1000, 5000 * attempt));
          continue;
        }
        throw new RateLimitedError("HTTP 429 (persistent)");
      }
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      return Buffer.from(await response.arrayBuffer());
    } catch (error) {
      if (error instanceof RateLimitedError) throw error;
      if (attempt === attempts) throw error;
      await delay(5000 * attempt);
    }
  }
  throw new Error("unreachable");
}

let failures = 0;
let rateLimited = false;
for (const entry of DAYS) {
  const id = String(entry.day).padStart(2, "0");
  const pgmPath = path.join(assetsDir, `day${id}.pgm`);
  if (existsSync(pgmPath)) {
    continue;
  }
  if (rateLimited) {
    console.error(`day${id}: skipped (rate-limited earlier in this run)`);
    continue;
  }

  const ext = entry.file.toLowerCase().endsWith(".png" ) ? "png" : "jpg";
  const downloadPath = path.join(tmpDir, `day${id}.${ext}`);
  try {
    console.log(`day${id}: downloading ${entry.motif}`);
    const bytes = await downloadWithRetry(entry.url);
    writeFileSync(downloadPath, bytes);
  } catch (error) {
    console.error(`day${id}: download failed: ${error.message}`);
    failures++;
    if (error instanceof RateLimitedError) {
      rateLimited = true;
      console.error("Wikimedia is rate-limiting this IP; stopping. Re-run the script in ~10 minutes to resume.");
    }
    continue;
  }

  try {
    execFileSync("magick", [
      downloadPath,
      "-resize", "600x800^",
      "-gravity", "center",
      "-extent", "600x800",
      "-colorspace", "Gray",
      "-dither", "FloydSteinberg",
      "-colors", "8",
      "-depth", "8",
      pgmPath
    ]);
    console.log(`day${id}: wrote ${path.relative(process.cwd(), pgmPath)}`);
  } catch (error) {
    console.error(`day${id}: magick failed: ${error.message}`);
    failures++;
  } finally {
    rmSync(downloadPath, { force: true });
  }
  await delay(3000 + Math.floor(Math.random() * 2000));
}

rmSync(tmpDir, { recursive: true, force: true });

const sources = [
  "# Advent Calendar Image Sources",
  "",
  "All images are vintage Christmas postcard scans from Wikimedia Commons",
  "(https://commons.wikimedia.org/wiki/Category:Christmas_postcards),",
  "each explicitly tagged Public domain on its Commons file page.",
  "Fetched and converted to 600x800 dithered monochrome PGM by",
  "scripts/fetch-advent-images.mjs.",
  ""
];
for (const entry of DAYS) {
  const id = String(entry.day).padStart(2, "0");
  sources.push(`- day${id}.pgm — ${entry.motif}`);
  sources.push(`  Source: https://commons.wikimedia.org/wiki/${encodeURIComponent(entry.file.replace(/ /g, "_"))}`);
  sources.push(`  Original: ${entry.url}`);
  sources.push(`  License: Public domain`);
  sources.push("");
}
writeFileSync(path.join(assetsDir, "SOURCES.md"), sources.join("\n"));

if (failures > 0) {
  console.error(`${failures} day(s) missing; re-run to retry the missing ones.`);
  process.exit(1);
}
const missing = DAYS.filter((entry) => !existsSync(path.join(assetsDir, `day${String(entry.day).padStart(2, "0")}.pgm`)));
if (missing.length > 0) {
  console.error(`${missing.length} day(s) missing: ${missing.map((m) => m.day).join(", ")}`);
  process.exit(1);
}
console.log("All 24 advent PGMs present.");
