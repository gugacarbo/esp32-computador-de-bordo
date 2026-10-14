// playwright.firmware.config.ts — configuração EXCLUSIVA dos testes de painel do firmware
// (SPEC-0003, ADR-0005). Não altera a configuração Playwright da raiz nem o app Bun.
// Uso: bunx playwright test -c playwright.firmware.config.ts
import { defineConfig } from "@playwright/test";

export default defineConfig({
	testDir: "./apps/firmware/web-tests",
	// O HTML é extraído do header a cada execução; nada de cache entre arquivos.
	fullyParallel: false,
	forbidOnly: Boolean(process.env.CI),
	retries: process.env.CI ? 1 : 0,
	reporter: process.env.CI ? "github" : "list",
	// Timeout folgado por causa do caso de resposta lenta do servidor de teste.
	timeout: 60_000,
	expect: { timeout: 10_000 },
	use: {},
});
