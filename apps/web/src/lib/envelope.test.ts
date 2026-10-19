import { describe, expect, test } from "bun:test";
import {
	asEnvelope,
	derivePanelState,
	formatSensorValue,
	PANEL_TEXT,
	parseEnvelopeText,
	SENSOR_KEYS,
	SENSOR_STATUSES,
	type SensorKey,
	type SensorReading,
	type SensorStatus,
	sensorStatusInfo,
	type TelemetryEnvelope,
	type VoltageReading,
	voltageTitle,
} from "./envelope";

function reading(overrides: Record<string, unknown> = {}): SensorReading {
	return {
		value: 1000,
		valid: true,
		status: "ok",
		updatedAtMs: 1234,
		ageMs: 50,
		...(overrides as Partial<SensorReading>),
	};
}

function envelopeJson(overrides: Record<string, unknown> = {}): string {
	return JSON.stringify({
		uptimeMs: 9999,
		btConnected: true,
		adapterReady: true,
		ecuResponding: true,
		obdConnected: true,
		rpm: reading({ value: 2345 }),
		speedKph: reading({ value: 87 }),
		coolantC: reading({ value: 91.4 }),
		throttlePercent: reading({ value: 22.6 }),
		mapKpa: reading({ value: 48.2 }),
		voltage: { ...reading({ value: 13.82 }), source: "adapter" },
		...overrides,
	});
}

describe("contrato", () => {
	test("6 sensores e 6 estados", () => {
		const keys: SensorKey[] = [...SENSOR_KEYS];
		expect(keys).toEqual([
			"rpm",
			"speedKph",
			"coolantC",
			"throttlePercent",
			"mapKpa",
			"voltage",
		]);
		expect(SENSOR_STATUSES).toHaveLength(6);
	});

	test("leitura de tensão carrega a origem", () => {
		const parsed = parseEnvelopeText(envelopeJson());
		const voltage: VoltageReading | undefined = parsed?.voltage;
		expect(voltage?.source).toBe("adapter");
	});
});

describe("parseEnvelopeText", () => {
	test("aceita o envelope válido do firmware", () => {
		const parsed = parseEnvelopeText(envelopeJson());
		expect(parsed).not.toBeNull();
		const env = parsed as TelemetryEnvelope;
		expect(env.uptimeMs).toBe(9999);
		expect(env.obdConnected).toBe(true);
		expect(env.rpm.value).toBe(2345);
		expect(env.voltage.source).toBe("adapter");
	});

	test("rejeita JSON malformado", () => {
		expect(parseEnvelopeText("{invalido")).toBeNull();
		expect(parseEnvelopeText("")).toBeNull();
	});

	test("rejeita não-objetos", () => {
		expect(parseEnvelopeText("null")).toBeNull();
		expect(parseEnvelopeText("[1,2]")).toBeNull();
		expect(parseEnvelopeText("42")).toBeNull();
	});

	test("rejeita flags com tipo errado", () => {
		expect(parseEnvelopeText(envelopeJson({ btConnected: 1 }))).toBeNull();
		expect(parseEnvelopeText(envelopeJson({ uptimeMs: "9999" }))).toBeNull();
	});

	test("btSearchStopped: true e false são lidos como estão", () => {
		expect(
			parseEnvelopeText(envelopeJson({ btSearchStopped: true }))
				?.btSearchStopped,
		).toBe(true);
		expect(
			parseEnvelopeText(envelopeJson({ btSearchStopped: false }))
				?.btSearchStopped,
		).toBe(false);
	});

	test("btSearchStopped ausente (firmware antigo) vale false", () => {
		const legacy = JSON.parse(envelopeJson());
		delete legacy["btSearchStopped"];
		expect(asEnvelope(legacy)?.btSearchStopped).toBe(false);
	});

	test("rejeita btSearchStopped com tipo errado", () => {
		expect(
			parseEnvelopeText(envelopeJson({ btSearchStopped: "true" })),
		).toBeNull();
		expect(parseEnvelopeText(envelopeJson({ btSearchStopped: 1 }))).toBeNull();
		expect(
			parseEnvelopeText(envelopeJson({ btSearchStopped: null })),
		).toBeNull();
	});

	test("rejeita sensor ausente ou com status inválido", () => {
		const missing = JSON.parse(envelopeJson());
		delete missing["rpm"];
		expect(asEnvelope(missing)).toBeNull();
		expect(
			parseEnvelopeText(envelopeJson({ rpm: reading({ status: "bogus" }) })),
		).toBeNull();
	});

	test("rejeita inconsistência valid/status", () => {
		expect(
			parseEnvelopeText(
				envelopeJson({ rpm: reading({ valid: true, status: "stale" }) }),
			),
		).toBeNull();
		expect(
			parseEnvelopeText(
				envelopeJson({ rpm: reading({ valid: false, status: "ok" }) }),
			),
		).toBeNull();
	});

	test("rejeita value não-numérico e updatedAtMs inválido", () => {
		const nanPayload = JSON.parse(envelopeJson());
		nanPayload["rpm"]["value"] = Number.NaN;
		expect(asEnvelope(nanPayload)).toBeNull();
		expect(
			parseEnvelopeText(envelopeJson({ rpm: reading({ value: "1000" }) })),
		).toBeNull();
		expect(
			parseEnvelopeText(
				envelopeJson({ rpm: reading({ updatedAtMs: "ontem" }) }),
			),
		).toBeNull();
	});

	test("aceita value nulo com status não-ok", () => {
		const parsed = parseEnvelopeText(
			envelopeJson({
				rpm: {
					value: null,
					valid: false,
					status: "unavailable",
					updatedAtMs: null,
					ageMs: null,
				},
			}),
		);
		expect(parsed?.rpm.value).toBeNull();
	});

	test("rejeita source de tensão desconhecido", () => {
		const bad = JSON.parse(envelopeJson());
		bad["voltage"]["source"] = "usb";
		expect(asEnvelope(bad)).toBeNull();
	});

	test("aceita source ecu", () => {
		const ecu = JSON.parse(envelopeJson());
		ecu["voltage"]["source"] = "ecu";
		expect(asEnvelope(ecu)?.voltage.source).toBe("ecu");
	});
});

describe("derivePanelState", () => {
	test("cobre as fases da sessão", () => {
		expect(
			derivePanelState({
				btConnected: false,
				adapterReady: false,
				ecuResponding: false,
			}),
		).toBe("disconnected");
		expect(
			derivePanelState({
				btConnected: true,
				adapterReady: false,
				ecuResponding: false,
			}),
		).toBe("adapter");
		expect(
			derivePanelState({
				btConnected: true,
				adapterReady: true,
				ecuResponding: false,
			}),
		).toBe("ecu");
		expect(
			derivePanelState({
				btConnected: true,
				adapterReady: true,
				ecuResponding: true,
			}),
		).toBe("connected");
	});
});

describe("formatSensorValue", () => {
	test("tensão com 1 casa, demais inteiros", () => {
		expect(formatSensorValue("voltage", 13.82)).toBe("13.8");
		expect(formatSensorValue("rpm", 2345.6)).toBe("2346");
		expect(formatSensorValue("speedKph", 87.4)).toBe("87");
		expect(formatSensorValue("coolantC", 91.5)).toBe("92");
		expect(formatSensorValue("throttlePercent", 22.6)).toBe("23");
		expect(formatSensorValue("mapKpa", 48.2)).toBe("48");
	});
});

describe("sensorStatusInfo", () => {
	test("mapeia todos os estados para pt-BR", () => {
		const labels: Record<string, string> = {};
		const statuses: SensorStatus[] = [
			"ok",
			"stale",
			"timeout",
			"error",
			"unsupported",
			"unavailable",
		];
		for (const status of statuses) {
			const info = sensorStatusInfo(
				reading({ valid: status === "ok", status }),
			);
			labels[status] = info.label;
		}
		expect(labels).toEqual({
			ok: "OK",
			stale: "Desatualizado",
			timeout: "Falha na leitura",
			error: "Falha na leitura",
			unsupported: "Não suportado",
			unavailable: "Sem leitura",
		});
	});
});

describe("textos", () => {
	test("painel cobre os 6 estados em pt-BR", () => {
		expect(PANEL_TEXT).toEqual({
			loading: "Carregando dados",
			disconnected: "OBD desconectado",
			adapter: "Inicializando adaptador",
			ecu: "ECU sem resposta",
			connected: "OBD conectado",
			"comm-error": "Sem comunicação com o ESP32",
		});
	});

	test("título da tensão segue a origem", () => {
		expect(voltageTitle("ecu")).toBe("Tensão da ECU");
		expect(voltageTitle("adapter")).toBe("Tensão do adaptador");
	});
});
