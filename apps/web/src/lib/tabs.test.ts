import { describe, expect, test } from "bun:test";
import { DEFAULT_TAB_ID, TAB_LABELS, TABS } from "./tabs";

describe("TABS", () => {
	test("três abas, Telemetria primeiro", () => {
		expect([...TABS]).toEqual(["telemetria", "conexao", "ajustes"]);
	});

	test("rótulos pt-BR para a barra de abas", () => {
		expect(TAB_LABELS.telemetria).toBe("Telemetria");
		expect(TAB_LABELS.conexao).toBe("Conexão");
		expect(TAB_LABELS.ajustes).toBe("Ajustes");
	});

	test("aba inicial é a Telemetria", () => {
		expect(DEFAULT_TAB_ID).toBe("telemetria");
	});
});
