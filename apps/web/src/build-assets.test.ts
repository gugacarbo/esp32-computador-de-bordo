import { beforeAll, expect, test } from "bun:test";
import { spawnSync } from "node:child_process";
import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import path from "node:path";

const webDir = path.resolve(import.meta.dir, "..");
const repoDir = path.resolve(webDir, "../..");
const stagedDir = path.join(repoDir, "apps/firmware/data");

function run(command: string, args: string[], cwd: string): void {
	const result = spawnSync(command, args, {
		cwd,
		encoding: "utf8",
		env: { ...process.env, NODE_ENV: "production" },
	});
	if (result.status !== 0) {
		throw new Error(`${command} ${args.join(" ")} falhou:\n${result.stderr}`);
	}
}

function treeBytes(directory: string): number {
	return readdirSync(directory, { withFileTypes: true }).reduce(
		(total, entry) => {
			const entryPath = path.join(directory, entry.name);
			return (
				total +
				(entry.isDirectory() ? treeBytes(entryPath) : statSync(entryPath).size)
			);
		},
		0,
	);
}

beforeAll(() => {
	run(process.execPath, ["run", "build"], webDir);
	run("node", ["scripts/stage.mjs"], webDir);
});

test("production stage ships separate CSS and JavaScript assets only as gzip", () => {
	const html = readFileSync(path.join(webDir, "dist/index.html"), "utf8");
	const cssAssets = [...html.matchAll(/href=["']([^"']+\.css)["']/g)].map(
		(match) => match[1],
	);
	const jsAssets = [...html.matchAll(/src=["']([^"']+\.js)["']/g)].map(
		(match) => match[1],
	);

	expect(cssAssets.length).toBeGreaterThan(0);
	expect(jsAssets.length).toBeGreaterThan(0);

	for (const asset of [...cssAssets, ...jsAssets]) {
		const relativePath = new URL(asset, "http://esp32.local/").pathname.slice(
			1,
		);
		const distPath = path.join(webDir, "dist", relativePath);
		expect(existsSync(distPath)).toBe(true);
		expect(existsSync(`${distPath}.gz`)).toBe(true);
		// O firmware serve sempre o .gz (ADR-0010): o original não vai para o LittleFS.
		const stagedPath = path.join(stagedDir, relativePath);
		expect(existsSync(`${stagedPath}.gz`)).toBe(true);
		expect(existsSync(stagedPath)).toBe(false);
	}
	expect(treeBytes(stagedDir)).toBeLessThanOrEqual(448 * 1024);
});
