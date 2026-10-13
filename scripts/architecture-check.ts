import { readdir } from "node:fs/promises";
import { join } from "node:path";

const sourceFiles: string[] = [];
const sourceRoots = ["apps", "packages"];

async function collect(directory: string): Promise<void> {
	let entries;
	try {
		entries = await readdir(directory, { withFileTypes: true });
	} catch (error) {
		if (error instanceof Error && "code" in error && error.code === "ENOENT") return;
		throw error;
	}
	for (const entry of entries) {
		const path = join(directory, entry.name);
		if (entry.isDirectory()) {
			await collect(path);
		} else if (entry.isFile() && /\.tsx?$/.test(entry.name)) {
			sourceFiles.push(path);
		}
	}
}

for (const root of sourceRoots) await collect(root);

if (sourceFiles.length === 0) {
		process.stdout.write("Nenhum arquivo TypeScript/TSX encontrado em apps ou packages.\n");
	process.exit(0);
}

const result = Bun.spawn(
	["bunx", "depcruise", "--config", ".dependency-cruiser.cjs", ...sourceFiles],
	{ stdin: "inherit", stdout: "inherit", stderr: "inherit" },
);
process.exit(await result.exited);
