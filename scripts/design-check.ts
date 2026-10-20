import { readdir, readFile } from "node:fs/promises";
import { join, relative, sep } from "node:path";

/** Violação encontrada: caminho relativo à raiz, linha (1-based) e mensagem em português. */
export interface Violacao {
	arquivo: string;
	linha: number;
	mensagem: string;
}

/** Uso cru de elemento/atributo proibido, com o trecho da linha para casar exceções. */
interface Uso extends Violacao {
	trecho: string;
}

/**
 * Exceções deliberadas à regra de uso de primitivas. `padrao` é um trecho que
 * precisa aparecer na linha; `motivo` explica por que o uso fica fora do padrão.
 */
export interface Excecao {
	arquivo: string;
	padrao: string;
	motivo: string;
}

export const EXCECOES: Excecao[] = [
	{
		arquivo: "components/ScannerReconnect.tsx",
		padrao: 'role="alert"',
		motivo:
			"Falha ao procurar o scanner precisa ser anunciada imediatamente a leitores de tela.",
	},
	{
		arquivo: "components/DiagnosticsPanel.tsx",
		padrao: 'role="alert"',
		motivo:
			"Erro de diagnóstico e aviso de entradas descartadas do log precisam de anúncio imediato.",
	},
	{
		arquivo: "components/WifiPanel.tsx",
		padrao: 'role="alert"',
		motivo:
			"Feedback de ação (sucesso ou erro) do Wi-Fi é anunciado imediatamente; o tom pode ser ok, então o papel é explícito.",
	},
	{
		arquivo: "components/BluetoothPanel.tsx",
		padrao: 'role="alert"',
		motivo:
			"Feedback de ação (sucesso ou erro) do Bluetooth é anunciado imediatamente; o tom pode ser ok, então o papel é explícito.",
	},
];

const RAIZ = join(import.meta.dir, "..");
const SRC = "apps/web/src";
const UI = `${SRC}/components/ui`;

const PALETA_TAILWIND =
	/\b(?:bg|text|border|ring|outline|fill|stroke|from|via|to|divide|placeholder|accent|caret|shadow|decoration)-(?:slate|gray|zinc|neutral|stone|red|orange|amber|yellow|lime|green|emerald|teal|cyan|sky|blue|indigo|violet|purple|fuchsia|pink|rose)-\d{2,3}\b/g;
const ARBITRARIO_COR =
	/\b(?:bg|text|border|ring|outline|fill|stroke|from|via|to|divide|placeholder|accent|caret|shadow|decoration)-\[(?:#|rgba?\(|hsla?\(|oklch\()[^\]]*\]/g;
const LITERAL_COR = /#[0-9a-fA-F]{3,8}\b|\brgba?\(/g;
const LINHA_DE_CLASSE = /className|\bcn\(|\bclass(?:Name)?\s*[:=]/;

const PRIMITIVAS = /<(button|input|select|textarea)(?=[\s/>])/g;
const ROLE_ALERT = /role="alert"/g;

function normalizar(caminho: string): string {
	return caminho.split(sep).join("/");
}

/** Verifica cores cruas: paleta Tailwind, valores arbitrários e literais em className. */
export function checkTokens(source: string, arquivo: string): Violacao[] {
	const caminho = normalizar(arquivo);
	if (!/\.tsx?$/.test(caminho)) return [];
	if (/(^|\/)index\.css$/.test(caminho)) return [];
	if (/\.test\./.test(caminho) || /\.stories\.tsx$/.test(caminho)) return [];

	const violacoes: Violacao[] = [];
	source.split("\n").forEach((texto, indice) => {
		const linha = indice + 1;
		const achados: string[] = [];
		for (const regex of [PALETA_TAILWIND, ARBITRARIO_COR]) {
			for (const match of texto.matchAll(regex)) achados.push(match[0]);
		}
		if (achados.length === 0 && LINHA_DE_CLASSE.test(texto)) {
			for (const match of texto.matchAll(LITERAL_COR)) achados.push(match[0]);
		}
		for (const achado of achados) {
			violacoes.push({
				arquivo: caminho,
				linha,
				mensagem: `cor crua "${achado}": use um token semântico definido em index.css`,
			});
		}
	});
	return violacoes;
}

/** Coleta todos os usos crus de primitivas e `role="alert"`, sem aplicar exceções. */
function coletarUsos(source: string, arquivo: string): Uso[] {
	const caminho = normalizar(arquivo);
	const usos: Uso[] = [];
	source.split("\n").forEach((texto, indice) => {
		const linha = indice + 1;
		for (const match of texto.matchAll(PRIMITIVAS)) {
			usos.push({
				arquivo: caminho,
				linha,
				trecho: texto,
				mensagem: `<${match[1]}> cru: use o componente de components/ui`,
			});
		}
		for (const _match of texto.matchAll(ROLE_ALERT)) {
			usos.push({
				arquivo: caminho,
				linha,
				trecho: texto,
				mensagem: 'role="alert" literal: use Alert de components/ui (tom error/warn já anuncia)',
			});
		}
	});
	return usos;
}

/** Verifica uso de primitivas fora de components/ui, descontando as exceções. */
export function checkUsage(
	source: string,
	arquivo: string,
	excecoes: Excecao[] = EXCECOES,
): Violacao[] {
	return coletarUsos(source, arquivo)
		.filter((uso) => !excecaoCobre(uso, excecoes))
		.map(({ arquivo, linha, mensagem }) => ({ arquivo, linha, mensagem }));
}

function excecaoCobre(uso: Uso, excecoes: Excecao[]): boolean {
	return excecoes.some(
		(excecao) =>
			uso.arquivo.endsWith(excecao.arquivo) && uso.trecho.includes(excecao.padrao),
	);
}

/** Acusa exceções que não casam com nenhum uso real (evita exceção esquecida). */
export function checkExcecoesObsoletas(
	usos: Uso[],
	excecoes: Excecao[] = EXCECOES,
): Violacao[] {
	return excecoes
		.filter(
			(excecao) =>
				!usos.some(
					(uso) =>
						uso.arquivo.endsWith(excecao.arquivo) &&
						uso.trecho.includes(excecao.padrao),
				),
		)
		.map((excecao) => ({
			arquivo: excecao.arquivo,
			linha: 1,
			mensagem: `exceção obsoleta para "${excecao.padrao}": remova de EXCECOES`,
		}));
}

/** Aceita `disabled` como prop explícita ou herdada de atributos HTML nativos. */
function aceitaDisabled(fonte: string): boolean {
	return (
		/\b(?:Button|Input|Select|Textarea)HTMLAttributes\b/.test(fonte) ||
		/\bdisabled\?:/.test(fonte) ||
		/[{,]\s*disabled\s*[,}=]/.test(fonte)
	);
}

/** Aceita tom/erro: prop `error`, literal "error" ou StatusTone (que inclui error). */
function aceitaErro(fonte: string): boolean {
	return /"error"|\berror\?:|\bStatusTone\b/.test(fonte);
}

function nomesDeStories(fonte: string): string[] {
	return [...fonte.matchAll(/export const (\w+)\s*(?::\s*\w+)?\s*=/g)].map(
		(match) => match[1] ?? "",
	);
}

/**
 * Verifica o par componente/stories de um arquivo em components/ui.
 * `fonteStories` é undefined quando o arquivo *.stories.tsx não existe.
 */
export function checkStories(
	arquivo: string,
	fonteComponente: string,
	fonteStories: string | undefined,
): Violacao[] {
	const caminho = normalizar(arquivo);
	const nome = caminho.replace(/^.*\//, "").replace(/\.tsx$/, "");
	const caminhoStories = caminho.replace(/\.tsx$/, ".stories.tsx");

	if (fonteStories === undefined) {
		return [
			{
				arquivo: caminho,
				linha: 1,
				mensagem: `componente sem ${nome}.stories.tsx ao lado`,
			},
		];
	}

	const violacoes: Violacao[] = [];
	if (!/tags:\s*\[\s*"autodocs"\s*\]/.test(fonteStories)) {
		violacoes.push({
			arquivo: caminhoStories,
			linha: 1,
			mensagem: 'stories sem tags: ["autodocs"]',
		});
	}

	const nomes = nomesDeStories(fonteStories);
	if (aceitaDisabled(fonteComponente) && !nomes.some((n) => n.includes("Desabilitad"))) {
		violacoes.push({
			arquivo: caminhoStories,
			linha: 1,
			mensagem: 'componente aceita disabled mas não tem story "Desabilitada*"',
		});
	}
	if (
		aceitaErro(fonteComponente) &&
		!nomes.some((n) => n.includes("Erro") || n.includes("Falha"))
	) {
		violacoes.push({
			arquivo: caminhoStories,
			linha: 1,
			mensagem: 'componente aceita erro mas não tem story "Falha" ou "Erro*"',
		});
	}
	return violacoes;
}

async function listarArquivos(diretorio: string): Promise<string[]> {
	const entradas = await readdir(diretorio, { withFileTypes: true });
	const arquivos: string[] = [];
	for (const entrada of entradas) {
		const caminho = join(diretorio, entrada.name);
		if (entrada.isDirectory()) {
			arquivos.push(...(await listarArquivos(caminho)));
		} else if (entrada.isFile()) {
			arquivos.push(caminho);
		}
	}
	return arquivos;
}

async function lerSeExistir(caminho: string): Promise<string | undefined> {
	try {
		return await readFile(caminho, "utf8");
	} catch (error) {
		if (error instanceof Error && "code" in error && error.code === "ENOENT") return undefined;
		throw error;
	}
}

async function main(): Promise<number> {
	const arquivos = (await listarArquivos(join(RAIZ, SRC)))
		.map((caminho) => normalizar(relative(RAIZ, caminho)))
		.filter((caminho) => /\.tsx?$/.test(caminho))
		.sort();

	const violacoes: Violacao[] = [];
	const usos: Uso[] = [];

	for (const caminho of arquivos) {
		const fonte = await readFile(join(RAIZ, caminho), "utf8");
		violacoes.push(...checkTokens(fonte, caminho));
		if (!caminho.startsWith(`${UI}/`)) {
			usos.push(...coletarUsos(fonte, caminho));
		}
	}

	const usosNaoExcetuados = usos.filter((uso) => !excecaoCobre(uso, EXCECOES));
	violacoes.push(
		...usosNaoExcetuados.map(({ arquivo, linha, mensagem }) => ({ arquivo, linha, mensagem })),
	);
	violacoes.push(...checkExcecoesObsoletas(usos, EXCECOES));

	const componentesUi = arquivos.filter(
		(caminho) =>
			caminho.startsWith(`${UI}/`) &&
			caminho.endsWith(".tsx") &&
			!caminho.endsWith(".stories.tsx") &&
			!/\.test\./.test(caminho),
	);
	for (const caminho of componentesUi) {
		const fonte = await readFile(join(RAIZ, caminho), "utf8");
		const caminhoStories = caminho.replace(/\.tsx$/, ".stories.tsx");
		const fonteStories = await lerSeExistir(join(RAIZ, caminhoStories));
		violacoes.push(...checkStories(caminho, fonte, fonteStories));
	}

	if (violacoes.length === 0) {
		process.stdout.write(
			`design:check ✔ ${arquivos.length} arquivos verificados em ${SRC}\n`,
		);
		return 0;
	}

	for (const v of violacoes) {
		process.stdout.write(`${v.arquivo}:${v.linha} — ${v.mensagem}\n`);
	}
	process.stdout.write(`design:check ✘ ${violacoes.length} violação(ões)\n`);
	return 1;
}

if (import.meta.main) {
	process.exit(await main());
}
