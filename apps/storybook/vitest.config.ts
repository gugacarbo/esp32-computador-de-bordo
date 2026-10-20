import path from "node:path";
import { fileURLToPath } from "node:url";
import { storybookTest } from "@storybook/addon-vitest/vitest-plugin";
import { playwright } from "@vitest/browser-playwright";
import { defineConfig } from "vitest/config";

const dirname = path.dirname(fileURLToPath(import.meta.url));

export default defineConfig({
	plugins: [
		storybookTest({
			configDir: path.join(dirname, ".storybook"),
			// Porta fixa só para o teste (dev usa STORYBOOK_PORT=6606; visual usa 6616).
			storybookUrl: "http://localhost:6626",
			storybookScript: "bunx storybook dev -p 6626 --ci --no-open",
		}),
	],
	test: {
		name: "storybook",
		browser: {
			enabled: true,
			headless: true,
			provider: playwright(),
			instances: [{ browser: "chromium" }],
		},
		coverage: {
			provider: "v8",
			reporter: ["text", "html"],
			reportsDirectory: path.join(dirname, ".vitest-coverage"),
			// Os componentes ficam em apps/web (fora deste workspace); allowExternal permite incluí-los.
			allowExternal: true,
			include: ["../web/src/components/ui/**/*.tsx"],
			// Testes SSR de componentes testam via bun; não são código de UI produzido.
			exclude: ["**/*.stories.tsx", "**/*.test.tsx"],
			// Medido com os stories atuais: ~100% linhas/funções/instruções e ~95% ramos.
			// Limite de 90% dá margem para pequenas variações; ramos ficam um pouco abaixo
			// por causa de prefixos/título opcionais do Alert e do Stat sem story específica.
			thresholds: {
				lines: 90,
				functions: 90,
				branches: 90,
				statements: 90,
			},
		},
	},
});
