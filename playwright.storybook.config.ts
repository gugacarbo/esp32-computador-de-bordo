// playwright.storybook.config.ts — regressão visual das stories do Storybook (ADR-0005 / design).
// Uso: bun run design:visual (atualizar snapshots: bun run design:visual -- --update-snapshots).
import { execSync } from "node:child_process";
import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { defineConfig } from "@playwright/test";

const port = Number(process.env.STORYBOOK_VISUAL_PORT ?? 6616);
const baseURL = `http://127.0.0.1:${port}`;
const staticDir = fileURLToPath(
	new URL("./apps/storybook/storybook-static/", import.meta.url),
);

// A lista de stories vem do index.json do build estático e precisa existir ANTES de o
// Playwright carregar o spec (a coleta de testes roda antes do webServer e do globalSetup).
// Por isso o build roda aqui, no carregamento do config, só no processo principal
// (workers também carregam o config; TEST_WORKER_INDEX identifica o worker).
// Sempre reconstrói para que stories novas ou alteradas entrem na lista.
if (process.env.TEST_WORKER_INDEX === undefined) {
	execSync("bun run --filter '@esp32-computador-de-bordo/storybook' build", {
		stdio: "inherit",
	});
}
if (!existsSync(`${staticDir}index.json`)) {
	throw new Error(
		"storybook-static/index.json ausente: o build do Storybook falhou.",
	);
}

export default defineConfig({
	testDir: "./apps/storybook/visual",
	outputDir: "./apps/storybook/visual/test-results",
	fullyParallel: true,
	forbidOnly: Boolean(process.env.CI),
	retries: process.env.CI ? 1 : 0,
	reporter: process.env.CI ? "github" : "list",
	timeout: 60_000,
	snapshotPathTemplate: "{testDir}/__snapshots__/{projectName}/{arg}{ext}",
	expect: {
		toHaveScreenshot: {
			maxDiffPixelRatio: 0.01,
			animations: "disabled",
		},
	},
	use: {
		baseURL,
		trace: "on-first-retry",
	},
	webServer: {
		// Servidor estático mínimo (Bun.serve) sobre o build já feito acima.
		command: "bun apps/storybook/visual/serve.ts",
		url: `${baseURL}/iframe.html`,
		reuseExistingServer: !process.env.CI,
		timeout: 120_000,
		stdout: "ignore",
		stderr: "pipe",
	},
	projects: [
		{
			name: "mobile",
			use: {
				viewport: { width: 390, height: 844 },
				isMobile: true,
				hasTouch: true,
			},
		},
		{
			name: "desktop",
			use: {
				viewport: { width: 1280, height: 800 },
			},
		},
	],
});
