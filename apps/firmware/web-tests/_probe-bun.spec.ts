import { test } from "@playwright/test";

test("runtime probe", async () => {
	const g = globalThis as unknown as { Bun?: { serve?: unknown } };
	console.log(
		"RUNTIME",
		JSON.stringify({
			bun: typeof g.Bun,
			serve: typeof g.Bun?.serve,
			bunVersion: (process.versions as { bun?: string }).bun,
			execPath: process.execPath,
			node: process.version,
		}),
	);
});
