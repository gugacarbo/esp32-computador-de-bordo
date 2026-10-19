import { describe, expect, test } from "bun:test";
import {
	describeWifiError,
	parseWifiActionResponse,
	parseWifiStatus,
	utf8Length,
	validateWifiForm,
	WIFI_PATH,
	type WifiMode,
	wifiModeLabel,
} from "./wifi";

describe("parseWifiStatus", () => {
	test("aceita o payload válido de cada modo", () => {
		const modes: WifiMode[] = [
			"connecting",
			"station",
			"station-lost",
			"access-point",
		];
		for (const mode of modes) {
			expect(
				parseWifiStatus({ mode, configured: true, ssid: "Fone", ip: "" }),
			).toEqual({ mode, configured: true, ssid: "Fone", ip: "" });
		}
	});

	test("rejeita modo desconhecido", () => {
		expect(
			parseWifiStatus({ mode: "ap", configured: false, ssid: "", ip: "" }),
		).toBeNull();
	});

	test("rejeita campos ausentes ou de tipo errado", () => {
		expect(
			parseWifiStatus({
				mode: "station",
				configured: "sim",
				ssid: "a",
				ip: "",
			}),
		).toBeNull();
		expect(
			parseWifiStatus({ mode: "station", configured: true, ssid: 1, ip: "" }),
		).toBeNull();
		expect(
			parseWifiStatus({ mode: "station", configured: true, ssid: "a" }),
		).toBeNull();
	});

	test("rejeita não-objetos", () => {
		expect(parseWifiStatus(null)).toBeNull();
		expect(parseWifiStatus("station")).toBeNull();
		expect(parseWifiStatus(undefined)).toBeNull();
	});
});

describe("parseWifiActionResponse", () => {
	test("aceita sucesso com reinício", () => {
		expect(parseWifiActionResponse({ ok: true, restarting: true })).toEqual({
			ok: true,
			restarting: true,
		});
	});

	test("rejeita respostas fora do contrato", () => {
		expect(parseWifiActionResponse({ ok: false, error: "storage" })).toBeNull();
		expect(parseWifiActionResponse({ ok: true })).toBeNull();
		expect(parseWifiActionResponse(null)).toBeNull();
	});
});

describe("utf8Length", () => {
	test("conta bytes, não caracteres", () => {
		expect(utf8Length("abc")).toBe(3);
		expect(utf8Length("ação")).toBe(6);
		expect(utf8Length("")).toBe(0);
	});
});

describe("validateWifiForm", () => {
	test("aceita SSID e senha válidos", () => {
		expect(
			validateWifiForm({ ssid: "Meu celular", password: "12345678" }),
		).toBeNull();
	});

	test("aceita senha vazia", () => {
		expect(validateWifiForm({ ssid: "Meu celular", password: "" })).toBeNull();
	});

	test("exige SSID de 1 a 32 bytes", () => {
		expect(validateWifiForm({ ssid: "", password: "" })).toContain("SSID");
		expect(validateWifiForm({ ssid: "a".repeat(32), password: "" })).toBeNull();
		expect(
			validateWifiForm({ ssid: "a".repeat(33), password: "" }),
		).not.toBeNull();
	});

	test("conta o SSID em bytes UTF-8", () => {
		// 17 caracteres "ç" = 34 bytes: acima do limite mesmo com 17 caracteres.
		expect(
			validateWifiForm({ ssid: "ç".repeat(17), password: "" }),
		).not.toBeNull();
		// 16 caracteres "ç" = 32 bytes: no limite.
		expect(validateWifiForm({ ssid: "ç".repeat(16), password: "" })).toBeNull();
	});

	test("senha, quando informada, tem de 8 a 63 bytes", () => {
		expect(validateWifiForm({ ssid: "a", password: "1234567" })).not.toBeNull();
		expect(validateWifiForm({ ssid: "a", password: "12345678" })).toBeNull();
		expect(
			validateWifiForm({ ssid: "a", password: "x".repeat(63) }),
		).toBeNull();
		expect(
			validateWifiForm({ ssid: "a", password: "x".repeat(64) }),
		).not.toBeNull();
	});

	test("senha com acentos conta bytes", () => {
		// 5 caracteres "ã" somam 10 bytes: vale como senha de 8+ bytes.
		expect(validateWifiForm({ ssid: "a", password: "ã".repeat(5) })).toBeNull();
		// 3 caracteres "ã" somam 6 bytes: abaixo do mínimo.
		expect(
			validateWifiForm({ ssid: "a", password: "ã".repeat(3) }),
		).not.toBeNull();
	});
});

describe("wifiModeLabel", () => {
	test("rótulos em português para cada modo", () => {
		expect(wifiModeLabel("connecting")).toBe("Conectando ao hotspot");
		expect(wifiModeLabel("station")).toBe("Conectado ao hotspot");
		expect(wifiModeLabel("station-lost")).toBe("Hotspot perdido, reconectando");
		expect(wifiModeLabel("access-point")).toBe("Rede própria (Onix-MVP)");
	});
});

describe("describeWifiError", () => {
	test("403 indica senha do OTA incorreta", () => {
		expect(describeWifiError(403, { ok: false, error: "forbidden" })).toBe(
			"Senha do OTA incorreta",
		);
	});

	test("404 indica firmware sem suporte a Wi-Fi", () => {
		expect(describeWifiError(404, null)).toContain("indisponível");
	});

	test("códigos de erro do corpo viram mensagens", () => {
		expect(
			describeWifiError(400, { ok: false, error: "invalid-credentials" }),
		).toContain("inválidos");
		expect(
			describeWifiError(400, { ok: false, error: "invalid-json" }),
		).toContain("não entendeu");
		expect(describeWifiError(500, { ok: false, error: "storage" })).toContain(
			"gravar",
		);
	});

	test("corpo desconhecido cai na mensagem genérica com o status", () => {
		expect(describeWifiError(502, "<html>")).toBe(
			"Falha na requisição (HTTP 502).",
		);
		expect(describeWifiError(400, { error: "outro" })).toBe(
			"Falha na requisição (HTTP 400).",
		);
	});
});

test("WIFI_PATH é relativo à origem da página", () => {
	expect(WIFI_PATH).toBe("/api/wifi");
});
