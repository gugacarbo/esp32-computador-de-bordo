import { existsSync, statSync } from "node:fs";
import { basename, extname, join } from "node:path";

const stagedPaths = process.argv.slice(2);
const visualTests = new Set<string>();

for (const file of stagedPaths) {
	const segments = file.split(/[\\/]+/);
	if (
		file === "." ||
		file === ".." ||
		segments.some((segment) => segment === "." || segment === "..") ||
		/[?*{}\[\]]/.test(file) ||
		file.endsWith("/") ||
		(existsSync(file) && statSync(file).isDirectory())
	) {
		throw new Error(`test-visual-staged recusou path não-explícito: ${JSON.stringify(file)}`);
	}
	const appName = file.match(/^apps\/([^/]+)\//)?.[1];
	const testDirectory = appName ? join("apps", appName, "tests/e2e") : "tests/e2e";
	const visualTest = join(testDirectory, `${basename(file, extname(file))}.visual.spec.ts`);
	if (existsSync(visualTest) && !statSync(visualTest).isDirectory()) visualTests.add(visualTest);
}

if (visualTests.size === 0) {
	process.stdout.write("Nenhum teste visual associado aos arquivos staged.\n");
	process.exit(0);
}

const result = Bun.spawn(["bunx", "playwright", "test", ...visualTests], {
	stdin: "inherit",
	stdout: "inherit",
	stderr: "inherit",
});
process.exit(await result.exited);
