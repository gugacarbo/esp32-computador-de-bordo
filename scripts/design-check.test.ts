import { describe, expect, test } from "bun:test";
import {
	checkExcecoesObsoletas,
	checkStories,
	checkTokens,
	checkUsage,
	type Excecao,
} from "./design-check";

const ARQUIVO = "apps/web/src/components/Exemplo.tsx";

describe("checkTokens", () => {
	test("aceita tokens semânticos", () => {
		expect(checkTokens('<div className="bg-surface text-fg" />', ARQUIVO)).toEqual([]);
	});

	test("acusa cor da paleta Tailwind com linha correta", () => {
		const violacoes = checkTokens('const a = 1;\n<div className="bg-red-500" />', ARQUIVO);
		expect(violacoes).toHaveLength(1);
		expect(violacoes[0]?.linha).toBe(2);
		expect(violacoes[0]?.arquivo).toBe(ARQUIVO);
		expect(violacoes[0]?.mensagem).toContain("bg-red-500");
	});

	test("acusa paleta em prefixos de gradiente e borda", () => {
		expect(checkTokens('"from-sky-300 border-zinc-700"', ARQUIVO)).toHaveLength(2);
	});

	test("acusa valores arbitrários de cor", () => {
		expect(checkTokens('className="bg-[#1a2b3c]"', ARQUIVO)).toHaveLength(1);
		expect(checkTokens('className="text-[rgb(1,2,3)]"', ARQUIVO)).toHaveLength(1);
	});

	test("acusa hex e rgb literal dentro de linha de className", () => {
		expect(checkTokens('<p className="x" style={{ color: "#fff" }} />', ARQUIVO)).toHaveLength(1);
		expect(checkTokens('const c = cn("rgb(0, 0, 0)");', ARQUIVO)).toHaveLength(1);
	});

	test("ignora hex fora de className", () => {
		expect(checkTokens('const cor = "#fff";', ARQUIVO)).toEqual([]);
	});

	test("não olha index.css, testes nem stories", () => {
		const cru = 'className="bg-red-500"';
		expect(checkTokens(cru, "apps/web/src/index.css")).toEqual([]);
		expect(checkTokens(cru, "apps/web/src/lib/x.test.ts")).toEqual([]);
		expect(checkTokens(cru, "apps/web/src/components/ui/button.stories.tsx")).toEqual([]);
	});
});

describe("checkStories", () => {
	const UI = "apps/web/src/components/ui/botao.tsx";
	const COMPONENTE_SIMPLES = "export function Botao() { return null; }";
	const STORIES_OK = `
const meta = { tags: ["autodocs"] };
export const Primaria: Story = {};
export const Desabilitada: Story = { args: { disabled: true } };
`;

	test("acusa componente sem arquivo de stories", () => {
		const violacoes = checkStories(UI, COMPONENTE_SIMPLES, undefined);
		expect(violacoes).toHaveLength(1);
		expect(violacoes[0]?.arquivo).toBe(UI);
		expect(violacoes[0]?.mensagem).toContain("botao.stories.tsx");
	});

	test("aceita stories com autodocs e estados mínimos", () => {
		expect(checkStories(UI, COMPONENTE_SIMPLES, STORIES_OK)).toEqual([]);
	});

	test("acusa stories sem tag autodocs", () => {
		const semTag = STORIES_OK.replace('tags: ["autodocs"]', "tags: []");
		const violacoes = checkStories(UI, COMPONENTE_SIMPLES, semTag);
		expect(violacoes.map((v) => v.mensagem)).toEqual(['stories sem tags: ["autodocs"]']);
	});

	test("exige story Desabilitada quando o componente aceita disabled", () => {
		const componente = "export interface BotaoProps extends ButtonHTMLAttributes<HTMLButtonElement> {}";
		const semDesabilitada = 'const meta = { tags: ["autodocs"] };\nexport const Primaria: Story = {};';
		const violacoes = checkStories(UI, componente, semDesabilitada);
		expect(violacoes.some((v) => v.mensagem.includes("Desabilitada"))).toBe(true);
		expect(checkStories(UI, componente, STORIES_OK)).toEqual([]);
	});

	test("não exige Desabilitada quando disabled só aparece em classe Tailwind", () => {
		const componente = 'const c = "disabled:opacity-50";';
		const semDesabilitada = 'const meta = { tags: ["autodocs"] };\nexport const Primaria: Story = {};';
		expect(checkStories(UI, componente, semDesabilitada)).toEqual([]);
	});

	test("exige story de falha quando o componente aceita tom error", () => {
		const componente = 'export type Tom = "ok" | "error";';
		const semFalha = 'const meta = { tags: ["autodocs"] };\nexport const Padrao: Story = {};';
		const violacoes = checkStories(UI, componente, semFalha);
		expect(violacoes.some((v) => v.mensagem.includes("Falha"))).toBe(true);
		expect(
			checkStories(UI, componente, `${semFalha}\nexport const ComErro: Story = {};`),
		).toEqual([]);
	});
});

describe("checkUsage", () => {
	const SEM_EXCECAO: Excecao[] = [];

	test("aceita componentes de ui em vez de primitivas", () => {
		expect(checkUsage("<Button>Salvar</Button>", "apps/web/src/App.tsx", SEM_EXCECAO)).toEqual([]);
	});

	test("acusa button, input, select e textarea crus", () => {
		const src = "<button>a</button>\n<input />\n<select></select>\n<textarea />";
		const violacoes = checkUsage(src, "apps/web/src/App.tsx", SEM_EXCECAO);
		expect(violacoes.map((v) => v.linha)).toEqual([1, 2, 3, 4]);
	});

	test("não confunde nomes que só começam com primitiva", () => {
		expect(checkUsage("<buttonGroup />", "apps/web/src/App.tsx", SEM_EXCECAO)).toEqual([]);
	});

	test("acusa role=\"alert\" literal", () => {
		const violacoes = checkUsage('<div role="alert">x</div>', "apps/web/src/App.tsx", SEM_EXCECAO);
		expect(violacoes).toHaveLength(1);
		expect(violacoes[0]?.mensagem).toContain('role="alert"');
	});

	test("aplica exceção documentada pelo arquivo e padrão", () => {
		const excecoes: Excecao[] = [
			{ arquivo: "components/Aviso.tsx", padrao: 'role="alert"', motivo: "teste" },
		];
		const src = '<div role="alert" />';
		expect(checkUsage(src, "apps/web/src/components/Aviso.tsx", excecoes)).toEqual([]);
		expect(checkUsage(src, "apps/web/src/components/Outro.tsx", excecoes)).toHaveLength(1);
	});
});

describe("checkExcecoesObsoletas", () => {
	test("acusa exceção que não casa com nenhum uso", () => {
		const excecoes: Excecao[] = [{ arquivo: "components/X.tsx", padrao: 'role="alert"', motivo: "m" }];
		expect(checkExcecoesObsoletas([], excecoes)).toHaveLength(1);
	});

	test("não acusa exceção usada", () => {
		const excecoes: Excecao[] = [{ arquivo: "components/X.tsx", padrao: 'role="alert"', motivo: "m" }];
		const usos = [
			{ arquivo: "apps/web/src/components/X.tsx", linha: 3, mensagem: "m", trecho: '<Alert role="alert">' },
		];
		expect(checkExcecoesObsoletas(usos, excecoes)).toEqual([]);
	});
});
