#!/usr/bin/env node
/**
 * node scripts/release-nexus.mjs [--dry-run]
 *
 * Publishes the built archive to Nexus as a new version of the main file, with
 * the changelog, so installed copies are offered the update.
 *
 * It reuses Event Horizon's Nexus v3 client rather than writing a second one:
 * upload session, presigned PUT, filename safety, backoff and multipart are all
 * already solved and already proven against this API.
 *
 * IT REFUSES TO SHIP DIAGNOSTICS. 0.1.0 went out with debug.json set to the
 * development profile, which turns on Papyrus tracing and writes to the
 * player's Fallout4Custom.ini. make-release now forces it off, and this checks
 * the bytes of the archive it is about to upload rather than trusting that --
 * the packaging step and the upload step are allowed to disagree exactly once,
 * and this is where that gets caught.
 *
 * Nexus API key: NEXUSMODS_API_KEY, or the first line of ~/.nexusmods/api-key.
 * It is never printed.
 */
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import zlib from "node:zlib";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "..");

// Event Horizon's client, imported rather than copied: one implementation, one
// place for the next API quirk to be fixed.
const LIB = path.resolve(root, "..", "vortex-mod-monitor", "scripts", "lib");
const { nexusClient, findMainFile, vortexWouldOfferUpdate, isReleasableVersion } = await import(
  `file://${path.join(LIB, "nexusRelease.mjs").replace(/\\/g, "/")}`
);

const NEXUS = { gameDomain: "fallout4", modId: 109219, title: "Rapport" };
const dryRun = process.argv.includes("--dry-run");
const step = (m) => console.log(`\n> ${m}`);
const info = (m) => console.log(`  ${m}`);
const fail = (m) => {
  console.error(`\nFAILED: ${m}`);
  process.exit(1);
};

// ── version: CMakeLists is the only place that declares one ──────────────
const cmake = fs.readFileSync(path.join(root, "CMakeLists.txt"), "utf8");
const version = (cmake.match(/VERSION\s+(\d+\.\d+\.\d+)/) ?? [])[1];
if (!version) fail("no VERSION x.y.z in CMakeLists.txt");
if (!isReleasableVersion(version)) fail(`${version} is not a plain x.y.z`);

// ── notes: this version's section of CHANGELOG.md, and nothing else ──────
const changelog = fs.readFileSync(path.join(root, "CHANGELOG.md"), "utf8");
const sec = changelog.split(/^## /m).find((s) => s.trimStart().startsWith(version));
if (!sec) fail(`CHANGELOG.md has no "## ${version}" section`);
const notes = sec.slice(version.length).trim();

// ── the archive, and what is actually inside it ──────────────────────────
const zipName = `Rapport-${version}.zip`;
const zipPath = path.join(root, "build", "release", zipName);
if (!fs.existsSync(zipPath)) fail(`${zipPath} is missing — run scripts/make-release.ps1 first`);
const bytes = fs.readFileSync(zipPath);

/** Minimal zip reader: enough to pull one stored/deflated entry out by suffix. */
function zipEntry(buf, suffix) {
  for (let i = buf.length - 22; i >= 0; i--) {
    if (buf.readUInt32LE(i) !== 0x06054b50) continue;
    let off = buf.readUInt32LE(i + 16);
    const count = buf.readUInt16LE(i + 10);
    for (let n = 0; n < count; n++) {
      const nameLen = buf.readUInt16LE(off + 28);
      const extraLen = buf.readUInt16LE(off + 30);
      const commentLen = buf.readUInt16LE(off + 32);
      const name = buf.toString("utf8", off + 46, off + 46 + nameLen);
      const local = buf.readUInt32LE(off + 42);
      if (name.replace(/\\/g, "/").endsWith(suffix)) {
        const lnLen = buf.readUInt16LE(local + 26);
        const leLen = buf.readUInt16LE(local + 28);
        const start = local + 30 + lnLen + leLen;
        const comp = buf.readUInt16LE(local + 8);
        const size = buf.readUInt32LE(off + 20);
        const raw = buf.subarray(start, start + size);
        return comp === 0 ? raw : zlib.inflateRawSync(raw);
      }
      off += 46 + nameLen + extraLen + commentLen;
    }
  }
  return undefined;
}

step(`Checking what ${zipName} would ship`);
const dbgRaw = zipEntry(bytes, "F4SE/Plugins/Rapport/debug.json");
if (!dbgRaw) fail("debug.json is not in the archive — cannot prove diagnostics are off");
if (dbgRaw[0] === 0xef && dbgRaw[1] === 0xbb && dbgRaw[2] === 0xbf)
  fail("debug.json has a byte-order mark — the plugin's JSON parser cannot read that");
const dbg = JSON.parse(dbgRaw.toString("utf8"));
if (dbg.active !== "off") fail(`debug.json says active="${dbg.active}" — REFUSING to ship diagnostics on`);
if (dbg.profiles?.off?.papyrus?.enabled !== false) fail('the "off" profile does not disable papyrus');
info(`debug.json: active="off", papyrus disabled`);

const iniRaw = zipEntry(bytes, "F4SE/Plugins/Rapport.ini");
if (!iniRaw) fail("Rapport.ini is not in the archive");
const ini = iniRaw.toString("utf8");
for (const flag of ["DevMailbox", "DevConsole"]) {
  const m = ini.match(new RegExp(`^\\s*${flag}\\s*=\\s*(\\S+)`, "m"));
  if (!m) fail(`Rapport.ini has no ${flag}`);
  if (m[1] !== "0") fail(`Rapport.ini has ${flag}=${m[1]} — REFUSING to ship the dev channel open`);
}
info("Rapport.ini: DevMailbox=0, DevConsole=0");
info(`${zipName}: ${bytes.length} bytes`);

// ── what Nexus has now ───────────────────────────────────────────────────
function readApiKey() {
  const env = process.env.NEXUSMODS_API_KEY?.trim();
  if (env) return env;
  const f = path.join(os.homedir(), ".nexusmods", "api-key");
  if (!fs.existsSync(f)) fail("no NEXUSMODS_API_KEY and no ~/.nexusmods/api-key");
  return fs.readFileSync(f, "utf8").split(/\r?\n/)[0].trim();
}

step(`Reading Nexus ${NEXUS.gameDomain}/mods/${NEXUS.modId}`);
const client = nexusClient({ apiKey: readApiKey(), userAgent: `RapportRelease/${version}` });
const mod = await client.getMod(NEXUS.gameDomain, String(NEXUS.modId));
if (!mod?.id) fail("Nexus returned no mod id");
const { file, latest } = await findMainFile(client, mod.id);
info(`"${mod.name ?? ""}" (${mod.id}); main file "${file.name}" (${file.id}); latest ${latest.version}`);
if (!vortexWouldOfferUpdate(latest.version, version))
  fail(`${version} is not newer than ${latest.version} — installed copies would not be offered it`);

if (dryRun) {
  step("Dry run — nothing written. Notes that would be published:");
  console.log(notes);
  process.exit(0);
}

// ── upload ───────────────────────────────────────────────────────────────
step("Uploading");
const uploadId = await client.uploadArchive({ bytes, filename: zipName, onState: info });
const created = await client.createModFileVersion(file.id, {
  upload_id: uploadId,
  name: `${NEXUS.title} ${version}`,
  version,
  description: notes.split("\n").find((l) => l.trim()) ?? "",
  file_category: "main",
  primary_mod_manager_download: true,
  allow_mod_manager_download: true,
  update_mod_version: true,
  archive_existing_file: true,
  previous_version_id: latest.id,
});
info(`created version ${created?.version?.id ?? "?"}`);

await client.addChangelog(mod.id, version, notes.replace(/\*\*/g, "").replace(/`/g, ""));
info("changelog added");

const after = (await client.getModFileVersions(file.id))?.versions ?? [];
if (!after.some((v) => v.version === version))
  fail(`Nexus does not list ${version} on file ${file.id} — check the page`);
step(`Published ${version} — https://www.nexusmods.com/${NEXUS.gameDomain}/mods/${NEXUS.modId}`);
