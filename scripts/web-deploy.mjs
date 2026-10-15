#!/usr/bin/env node
// web-deploy.mjs — comando único: build apps/web -> stage p/ LittleFS -> build firmware -> uploadfs + upload.
//
// Uso:
//   bun run web:deploy [--port /dev/ttyUSB0] [--build-only] [--skip-fs] [--skip-fw] [--budget=KB]
//   WEB_DEPLOY_PORT ou PIO_UPLOAD_PORT define a porta quando --port é omitido.
//   --build-only valida o pipeline sem tocar na placa (build web + stage + build firmware).
//
// pio é resolvido em .venv/bin/pio (gotcha do repo) com fallback para o PATH.
import { spawnSync } from "node:child_process";
import { existsSync, statSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const webDir = path.join(root, "apps", "web");
const firmwareDir = path.join(root, "apps", "firmware");
const stagedHtml = path.join(firmwareDir, "data", "index.html");

const args = new Set(process.argv.slice(2));
const portArg = process.argv.find((a) => a.startsWith("--port="))?.split("=")[1];
const budgetArg = process.argv.find((a) => a.startsWith("--budget="))?.split("=")[1];
const port = portArg ?? process.env.WEB_DEPLOY_PORT ?? process.env.PIO_UPLOAD_PORT;
const buildOnly = args.has("--build-only");
const skipFs = args.has("--skip-fs");
const skipFw = args.has("--skip-fw");
const budgetBytes = (budgetArg ? Number(budgetArg) : 600) * 1024;

function fail(message) {
	console.error(`web:deploy ERRO: ${message}`);
	process.exit(1);
}

function step(label) {
	console.log(`\n=== web:deploy: ${label} ===`);
}

function run(cmd, runArgs, cwd) {
	const result = spawnSync(cmd, runArgs, { cwd, stdio: "inherit", shell: false });
	if (result.status !== 0) fail(`${cmd} ${runArgs.join(" ")} saiu com ${result.status}`);
}

function resolvePio() {
	const venvPio = path.join(root, ".venv", "bin", "pio");
	if (existsSync(venvPio)) return venvPio;
	return "pio";
}

if (!existsSync(path.join(webDir, "package.json"))) fail("apps/web ausente — rode a partir da raiz do repo");
if (!Number.isFinite(budgetBytes) || budgetBytes <= 0) fail("--budget inválido (KB numérico)");

// 1. Build do app web (bundle enxuto singlefile em apps/web/dist).
step("build apps/web");
run("bun", ["run", "build"], webDir);

// 2. Stage para o LittleFS do firmware + orçamento do bundle.
step("stage dist -> apps/firmware/data");
run("bun", ["run", "web:stage"], webDir);
if (!existsSync(stagedHtml)) fail("stage não gerou apps/firmware/data/index.html");
const size = statSync(stagedHtml).size;
console.log(`bundle staged: ${(size / 1024).toFixed(1)} KB (orçamento ${(budgetBytes / 1024).toFixed(0)} KB)`);
if (size > budgetBytes) {
	fail(`bundle acima do orçamento — enxute o app ou passe --budget=KB (partição LittleFS ~1,4 MB)`);
}

// 3. Build do firmware (valida LittleFS + WS compilando para o ESP32).
step("build firmware esp32doit-devkit-v1");
const pio = resolvePio();
console.log(`usando pio: ${pio}`);
run(pio, ["run", "-d", firmwareDir, "-e", "esp32doit-devkit-v1"], root);

if (buildOnly) {
	console.log("\nweb:deploy OK (--build-only: uploads pulados)");
	process.exit(0);
}

// 4. Gravação na placa: filesystem (painel) + firmware.
const uploadArgs = port ? ["--upload-port", port] : [];
if (!skipFs) {
	step("upload filesystem (LittleFS com o painel)");
	run(pio, ["run", "-d", firmwareDir, "-e", "esp32doit-devkit-v1", "-t", "uploadfs", ...uploadArgs], root);
}
if (!skipFw) {
	step("upload firmware");
	run(pio, ["run", "-d", firmwareDir, "-e", "esp32doit-devkit-v1", "-t", "upload", ...uploadArgs], root);
}

console.log("\nweb:deploy OK — painel em http://192.168.4.1 (WS na porta 81)");
