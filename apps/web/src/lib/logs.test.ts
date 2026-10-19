import { describe, expect, test } from "bun:test";
import {
	formatUptime,
	LOGS_PATH,
	parseLogsResponse,
	parseLogsText,
} from "./logs";

function logsJson(overrides: Record<string, unknown> = {}): string {
	return JSON.stringify({
		uptimeMs: 123456,
		dropped: 3,
		entries: [
			{ uptimeMs: 120000, level: "warn", tag: "bt", message: "falha" },
			{ uptimeMs: 121000, level: "debug", tag: "obd", message: "ok" },
		],
		...overrides,
	});
}

describe("parseLogsText", () => {
	test("aceita o payload válido do firmware", () => {
		const parsed = parseLogsText(logsJson());
		expect(parsed).not.toBeNull();
		expect(parsed?.uptimeMs).toBe(123456);
		expect(parsed?.dropped).toBe(3);
		expect(parsed?.entries).toHaveLength(2);
		expect(parsed?.entries[0]).toEqual({
			uptimeMs: 120000,
			level: "warn",
			tag: "bt",
			message: "falha",
		});
	});

	test("aceita lista vazia", () => {
		const parsed = parseLogsText(logsJson({ entries: [], dropped: 0 }));
		expect(parsed?.entries).toEqual([]);
	});

	test("aceita os quatro níveis", () => {
		const levels = ["error", "warn", "info", "debug"].map((level) =>
			parseLogsText(
				logsJson({
					entries: [{ uptimeMs: 1, level, tag: "t", message: "m" }],
				}),
			),
		);
		expect(levels.every((snapshot) => snapshot !== null)).toBe(true);
	});

	test("rejeita nível desconhecido", () => {
		expect(
			parseLogsText(
				logsJson({
					entries: [{ uptimeMs: 1, level: "fatal", tag: "t", message: "m" }],
				}),
			),
		).toBeNull();
	});

	test("rejeita JSON malformado e não-objetos", () => {
		expect(parseLogsText("{invalido")).toBeNull();
		expect(parseLogsText("")).toBeNull();
		expect(parseLogsText("null")).toBeNull();
		expect(parseLogsText("[1,2]")).toBeNull();
	});

	test("rejeita campos ausentes ou de tipo errado", () => {
		expect(parseLogsText(logsJson({ uptimeMs: "1" }))).toBeNull();
		expect(parseLogsText(logsJson({ dropped: -1 }))).toBeNull();
		expect(parseLogsText(logsJson({ dropped: 1.5 }))).toBeNull();
		expect(parseLogsText(logsJson({ entries: "x" }))).toBeNull();
		expect(
			parseLogsText(
				logsJson({
					entries: [{ uptimeMs: 1, level: "info", tag: 7, message: "m" }],
				}),
			),
		).toBeNull();
		expect(
			parseLogsText(
				logsJson({
					entries: [{ level: "info", tag: "t", message: "m" }],
				}),
			),
		).toBeNull();
	});

	test("uma entrada inválida invalida o payload inteiro", () => {
		const data = JSON.parse(logsJson());
		data.entries.push({ uptimeMs: 1, level: "nope", tag: "t", message: "m" });
		expect(parseLogsResponse(data)).toBeNull();
	});
});

describe("formatUptime", () => {
	test("formato mm:ss.d até 1 hora", () => {
		expect(formatUptime(0)).toBe("00:00.0");
		expect(formatUptime(59999)).toBe("00:59.9");
		expect(formatUptime(754500)).toBe("12:34.5");
	});

	test("a partir de 1 hora usa Hh mm:ss", () => {
		expect(formatUptime(3600000)).toBe("1h00:00");
		expect(formatUptime(3723000)).toBe("1h02:03");
	});

	test("valores inválidos viram zero", () => {
		expect(formatUptime(-5)).toBe("00:00.0");
		expect(formatUptime(Number.NaN)).toBe("00:00.0");
	});
});

test("LOGS_PATH é relativo à origem da página", () => {
	expect(LOGS_PATH).toBe("/api/logs");
});
