// Servidor estático mínimo do build do Storybook, usado pelo webServer do Playwright
// (playwright.storybook.config.ts). Sem dependências: só Bun.serve.
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("../storybook-static/", import.meta.url));
const port = Number(process.env.STORYBOOK_VISUAL_PORT ?? 6616);

Bun.serve({
	port,
	hostname: "127.0.0.1",
	async fetch(request) {
		const { pathname } = new URL(request.url);
		const relative = decodeURIComponent(pathname === "/" ? "/index.html" : pathname);
		const filePath = fileURLToPath(new URL(`.${relative}`, `file://${root}`));

		// Impede sair da pasta do build.
		if (!filePath.startsWith(root)) {
			return new Response("Not found", { status: 404 });
		}

		const file = Bun.file(filePath);
		if (!(await file.exists())) {
			return new Response("Not found", { status: 404 });
		}
		return new Response(file);
	},
});

console.log(`Storybook estático em http://127.0.0.1:${port}`);
