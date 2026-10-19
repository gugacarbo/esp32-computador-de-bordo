#!/usr/bin/env node
// web-deploy.mjs — comando único: build web -> stage assets p/ LittleFS -> build firmware -> uploadfs + upload.
//
// Uso:
//   bun run web:deploy [--port /dev/ttyUSB0 | --ota] [--build-only] [--skip-fs] [--skip-fw] [--budget=KB]
//   WEB_DEPLOY_PORT ou PIO_UPLOAD_PORT define a porta quando --port é omitido.
//   --ota grava por Wi-Fi (env `ota`, espota em 192.168.4.1): o host precisa estar no SoftAP
//   e a placa já precisa rodar um firmware com OTA gravado por USB (ADR-0009).
//   --build-only valida o pipeline sem tocar na placa (build web + stage + build firmware).
//
// pio é resolvido em .venv/bin/pio (gotcha do repo) com fallback para o PATH.
import { spawnSync } from "node:child_process";
import { existsSync, readdirSync, statSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const webDir = path.join(root, "apps", "web");
const firmwareDir = path.join(root, "apps", "firmware");
const stagedHtml = path.join(firmwareDir, "data", "index.html");
const stagedDataDir = path.join(firmwareDir, "data");

const args = new Set(process.argv.slice(2));
const portFlagIndex = process.argv.indexOf("--port");
const portArg =
	process.argv.find((a) => a.startsWith("--port="))?.split("=")[1] ??
	(portFlagIndex >= 0 ? process.argv[portFlagIndex + 1] : undefined);
const budgetArg = process.argv.find((a) => a.startsWith("--budget="))?.split("=")[1];
const port = portArg ?? process.env.WEB_DEPLOY_PORT ?? process.env.PIO_UPLOAD_PORT;
const buildOnly = args.has("--build-only");
const skipFs = args.has("--skip-fs");
const skipFw = args.has("--skip-fw");
const ota = args.has("--ota");
const uploadEnv = ota ? "ota" : "esp32doit-devkit-v1";
const otaHost = "192.168.4.1"; // upload_port do env `ota`
// Partição LittleFS de 512 KB (partitions_ota.csv); a folga cobre os metadados do LittleFS.
const budgetBytes = (budgetArg ? Number(budgetArg) : 448) * 1024;

function fail(message) {
	console.error(`web:deploy ERRO: ${message}`);
	process.exit(1);
}

function step(label) {
	console.log(`\n=== web:deploy: ${label} ===`);
}

function run(cmd, runArgs, cwd, env = {}) {
	const result = spawnSync(cmd, runArgs, {
		cwd,
		stdio: "inherit",
		shell: false,
		env: { ...process.env, ...env },
	});
	if (result.status !== 0) fail(`${cmd} ${runArgs.join(" ")} saiu com ${result.status}`);
}

function resolvePio() {
	const venvPio = path.join(root, ".venv", "bin", "pio");
	if (existsSync(venvPio)) return venvPio;
	return "pio";
}

// Após uma gravação OTA a placa reinicia e o host perde o SoftAP; espota não tenta de novo,
// então a próxima gravação só começa quando o HTTP da placa voltar a responder.
async function waitForBoard(timeoutMs = 90_000) {
	const deadline = Date.now() + timeoutMs;
	while (Date.now() < deadline) {
		try {
			const response = await fetch(`http://${otaHost}/api/data`, { signal: AbortSignal.timeout(2000) });
			if (response.ok) return;
		} catch {
			// placa reiniciando ou host ainda reconectando ao SoftAP
		}
		await new Promise((resolve) => setTimeout(resolve, 2000));
	}
	fail(`placa não respondeu em ${otaHost} após ${timeoutMs / 1000} s — reconecte o host ao SoftAP`);
}

function treeSize(directory) {
	return readdirSync(directory, { withFileTypes: true }).reduce((total, entry) => {
		const entryPath = path.join(directory, entry.name);
		return total + (entry.isDirectory() ? treeSize(entryPath) : statSync(entryPath).size);
	}, 0);
}

if (!existsSync(path.join(webDir, "package.json"))) fail("apps/web ausente — rode a partir da raiz do repo");
if (!Number.isFinite(budgetBytes) || budgetBytes <= 0) fail("--budget inválido (KB numérico)");
if (ota && portArg) fail("--ota e --port são exclusivos: o OTA grava em 192.168.4.1");

// 1. Build do app web (HTML e assets compactados em apps/web/dist).
step("build apps/web");
run("bun", ["run", "build"], webDir, { NODE_ENV: "production" });

// 2. Stage para o LittleFS do firmware + orçamento de todos os arquivos publicados.
step("stage dist -> apps/firmware/data");
run("bun", ["run", "web:stage"], webDir);
if (!existsSync(stagedHtml)) fail("stage não gerou apps/firmware/data/index.html");
const size = treeSize(stagedDataDir);
console.log(`arquivos staged: ${(size / 1024).toFixed(1)} KB (orçamento ${(budgetBytes / 1024).toFixed(0)} KB)`);
if (size > budgetBytes) {
	fail(`bundle acima do orçamento — enxute o app ou passe --budget=KB (partição LittleFS de 512 KB)`);
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

// 4. Gravação na placa: filesystem (painel) + firmware, por USB ou por Wi-Fi (--ota).
// No OTA cada gravação reinicia a placa; antes de cada uma, espera o HTTP da placa responder.
const uploadArgs = port && !ota ? ["--upload-port", port] : [];
const via = ota ? "OTA" : "USB";
if (!skipFs) {
	step(`upload filesystem via ${via} (LittleFS com o painel)`);
	if (ota) await waitForBoard();
	run(pio, ["run", "-d", firmwareDir, "-e", uploadEnv, "-t", "uploadfs", ...uploadArgs], root);
}
if (!skipFw) {
	step(`upload firmware via ${via}`);
	if (ota) await waitForBoard();
	run(pio, ["run", "-d", firmwareDir, "-e", uploadEnv, "-t", "upload", ...uploadArgs], root);
}

console.log("\nweb:deploy OK — painel em http://192.168.4.1 (WS na porta 81)");
