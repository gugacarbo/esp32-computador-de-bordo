import { existsSync, statSync } from "node:fs";
import { dirname, extname, join, parse } from "node:path";

const stagedPaths = process.argv.slice(2);

function assertExplicitFile(file: string): void {
	const segments = file.split(/[\\/]+/);
	if (
		file === "." ||
		file === ".." ||
		segments.some((segment) => segment === "." || segment === "..") ||
		/[?*{}\[\]]/.test(file) ||
		file.endsWith("/") ||
		(existsSync(file) && statSync(file).isDirectory())
	) {
		throw new Error(`test-staged recusou path não-explícito: ${JSON.stringify(file)}`);
	}
}

for (const stagedPath of stagedPaths) assertExplicitFile(stagedPath);

const testPaths = new Set<string>();
for (const stagedPath of stagedPaths) {
	// Fixtures/specs Playwright do firmware: validados via `firmware:test:web`,
	// nunca via `bun test` (o runner Bun quebra ao importar `@playwright/test`).
	if (stagedPath.includes("apps/firmware/web-tests/")) {
		continue;
	}
	if (/\.(test|spec)\.[cm]?[jt]sx?$/.test(stagedPath)) {
		if (existsSync(stagedPath)) testPaths.add(stagedPath);
		continue;
	}
	const parsed = parse(stagedPath);
	const stem = join(dirname(stagedPath), parsed.name);
	for (const suffix of [".test.ts", ".test.tsx", ".spec.ts", ".spec.tsx"]) {
		const candidate = `${stem}${suffix}`;
		if (existsSync(candidate) && !statSync(candidate).isDirectory()) testPaths.add(candidate);
	}
}

if (testPaths.size === 0) {
	process.stdout.write("Nenhum teste Bun diretamente afetado pelos arquivos staged.\n");
	process.exit(0);
}

const result = Bun.spawn(["bun", "test", ...testPaths], { stdin: "inherit", stdout: "inherit", stderr: "inherit" });
process.exit(await result.exited);
