// stage.mjs — copia a árvore de dist para apps/firmware/data (LittleFS do firmware)
// e imprime os tamanhos dos arquivos publicados.
import { cp, mkdir, readdir, rm, stat } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const webRoot = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const distHtml = path.join(webRoot, "dist", "index.html");
const distDir = path.join(webRoot, "dist");
const targetDir = path.join(webRoot, "..", "firmware", "data");
const targetHtml = path.join(targetDir, "index.html");
const generatedEntries = ["assets", "index.html", "index.html.gz"];

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

async function treeBytes(directory) {
	let total = 0;
	for (const entry of await readdir(directory, { withFileTypes: true })) {
		const entryPath = path.join(directory, entry.name);
		total += entry.isDirectory() ? await treeBytes(entryPath) : ((await sizeOf(entryPath)) ?? 0);
	}
	return total;
}

await mkdir(targetDir, { recursive: true });
for (const entry of generatedEntries) {
	await rm(path.join(targetDir, entry), { recursive: true, force: true });
}
for (const entry of await readdir(distDir)) {
	await cp(path.join(distDir, entry), path.join(targetDir, entry), { recursive: true });
}

const htmlSize = await sizeOf(distHtml);
const stagedSize = await sizeOf(targetHtml);
const stagedGzipSize = await sizeOf(`${targetHtml}.gz`);
const totalSize = await treeBytes(targetDir);

console.log(`web:stage dist/* -> ${path.relative(process.cwd(), targetDir)}`);
if (htmlSize !== null) console.log(`  dist/index.html: ${formatBytes(htmlSize)} (${htmlSize} bytes)`);
if (stagedGzipSize !== null) {
	console.log(`  staged index.html.gz: ${formatBytes(stagedGzipSize)} (${stagedGzipSize} bytes)`);
}
if (stagedSize !== null) console.log(`  staged HTML: ${formatBytes(stagedSize)} (${stagedSize} bytes)`);
console.log(`  stage total: ${formatBytes(totalSize)} (${totalSize} bytes)`);
