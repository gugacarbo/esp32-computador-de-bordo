import { describe, expect, test } from "bun:test";
import {
	describeBtError,
	isScanRunning,
	normalizeMac,
	parseBtConfigStatus,
	validateBtConfig,
} from "./btconfig";

const STATUS = {
	custom: true,
	name: "OBDII",
	pin: "1234",
	mac: "AA:BB:CC:DD:EE:FF",
	scan: {
		state: "done",
		devices: [
			{ name: "OBDII", mac: "11:22:33:44:55:66", rssi: -60 },
			{ name: "", mac: "77:88:99:AA:BB:CC", rssi: null },
		],
	},
};

describe("parseBtConfigStatus", () => {
	test("aceita o contrato completo", () => {
		const parsed = parseBtConfigStatus(STATUS);
		expect(parsed?.custom).toBe(true);
		expect(parsed?.scan.devices).toHaveLength(2);
		expect(parsed?.scan.devices[1]?.rssi).toBeNull();
	});

	test("recusa estado de busca desconhecido e campos com tipo errado", () => {
		expect(
			parseBtConfigStatus({ ...STATUS, scan: { state: "x", devices: [] } }),
		).toBeNull();
		expect(parseBtConfigStatus({ ...STATUS, custom: "true" })).toBeNull();
		expect(
			parseBtConfigStatus({
				...STATUS,
				scan: { state: "done", devices: [{ name: 1, mac: "", rssi: null }] },
			}),
		).toBeNull();
		expect(parseBtConfigStatus(null)).toBeNull();
	});
});

describe("validateBtConfig", () => {
	test("exige nome ou MAC", () => {
		expect(validateBtConfig({ name: "", pin: "", mac: "" })).not.toBeNull();
		expect(validateBtConfig({ name: "OBDII", pin: "", mac: "" })).toBeNull();
		expect(
			validateBtConfig({ name: "", pin: "", mac: "aa-bb-cc-dd-ee-ff" }),
		).toBeNull();
	});

	test("recusa MAC malformado e PIN longo", () => {
		expect(validateBtConfig({ name: "X", pin: "", mac: "AA:BB:CC" })).toContain(
			"MAC",
		);
		expect(
			validateBtConfig({ name: "X", pin: "12345678901234567", mac: "" }),
		).toContain("PIN");
	});
});

test("normalizeMac usa maiúsculas e dois-pontos", () => {
	expect(normalizeMac(" aa-bb-cc-dd-ee-0f ")).toBe("AA:BB:CC:DD:EE:0F");
});

test("isScanRunning só para pending e scanning", () => {
	expect(isScanRunning("pending")).toBe(true);
	expect(isScanRunning("scanning")).toBe(true);
	expect(isScanRunning("done")).toBe(false);
	expect(isScanRunning("busy")).toBe(false);
});

test("describeBtError traduz os códigos do firmware", () => {
	expect(describeBtError(404, null)).toContain("indisponível");
	expect(
		describeBtError(400, { ok: false, error: "invalid-config" }),
	).toContain("inválidos");
	expect(describeBtError(409, { ok: false, error: "busy" })).toContain(
		"andamento",
	);
	expect(describeBtError(500, null)).toBe("Falha na requisição (HTTP 500).");
});
