// stage.mjs — copia dist/index.html para apps/firmware/data/index.html
// (LittleFS do firmware) e imprime os tamanhos do bundle.
import { copyFile, mkdir, stat } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const webRoot = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const distHtml = path.join(webRoot, "dist", "index.html");
const distGz = `${distHtml}.gz`;
const targetDir = path.join(webRoot, "..", "firmware", "data");
const targetHtml = path.join(targetDir, "index.html");

function formatBytes(bytes) {
	if (bytes < 1024) return `${bytes} B`;
	return `${(bytes / 1024).toFixed(1)} KB`;
}

async function sizeOf(file) {
	try {
		return (await stat(file)).size;
	} catch {
		return null;
	}
}

await mkdir(targetDir, { recursive: true });
await copyFile(distHtml, targetHtml);

const htmlSize = await sizeOf(distHtml);
const gzSize = await sizeOf(distGz);
const stagedSize = await sizeOf(targetHtml);

console.log(`web:stage dist/index.html -> ${path.relative(process.cwd(), targetHtml)}`);
if (htmlSize !== null) console.log(`  dist/index.html: ${formatBytes(htmlSize)} (${htmlSize} bytes)`);
if (gzSize !== null) console.log(`  dist/index.html.gz: ${formatBytes(gzSize)} (${gzSize} bytes)`);
if (stagedSize !== null) console.log(`  staged: ${formatBytes(stagedSize)} (${stagedSize} bytes)`);
