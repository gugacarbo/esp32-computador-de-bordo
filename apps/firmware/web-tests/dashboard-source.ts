// dashboard-source.ts — util de teste: extrai o HTML REAL de apps/firmware/include/dashboard.h.
// Os testes Playwright da SPEC-0003 nunca usam uma cópia independente do painel: toda asserção roda
// sobre o literal bruto que o firmware serve em GET /. Nada aqui é usado em produção.
import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));

/** Caminho do cabeçalho do painel no firmware. */
export const DASHBOARD_HEADER_PATH = resolve(here, "..", "include", "dashboard.h");

const RAW_LITERAL = /R"HTML\(([\s\S]*?)\)HTML"/;

/**
 * Lê dashboard.h e devolve o documento HTML embutido no literal bruto `R"HTML(...)HTML"`.
 * Falha alto se o literal não existir: um teste que não extrai o header não é evidência.
 */
export function extractDashboardHtml(headerPath: string = DASHBOARD_HEADER_PATH): string {
	const source = readFileSync(headerPath, "utf8");
	const match = RAW_LITERAL.exec(source);
	if (!match) {
		throw new Error(`literal R"HTML(...)HTML" ausente em ${headerPath}`);
	}
	const html = match[1];
	if (html.trim().length === 0) {
		throw new Error(`literal HTML vazio em ${headerPath}`);
	}
	return html;
}

/** Recursos externos que a página não pode requisitar (SPEC-0003 §Conteúdo e apresentação). */
export const EXTERNAL_RESOURCE_PATTERN =
	/(?:https?:)?\/\/|@import|url\(\s*['"]?(?:https?:)?\/\/|<script[^>]+src=|<link[^>]+href=|integrity=/i;
