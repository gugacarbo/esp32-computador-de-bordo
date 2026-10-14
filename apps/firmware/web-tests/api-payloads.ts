// api-payloads.ts — util de teste: monta envelopes JSON da SPEC-0002 (válidos e inválidos).
// Só testes usam este arquivo; ele não descreve o firmware, apenas produz entradas controladas
// para o painel. Os payloads seguem exatamente o contrato de `GET /api/data`.
export type SensorStatus = "unavailable" | "unsupported" | "timeout" | "error" | "stale" | "ok";
export type VoltageSource = "adapter" | "ecu";
export type SensorKey =
	| "rpm"
	| "speedKph"
	| "coolantC"
	| "throttlePercent"
	| "mapKpa"
	| "voltage";

export const SENSOR_KEYS: readonly SensorKey[] = [
	"rpm",
	"speedKph",
	"coolantC",
	"throttlePercent",
	"mapKpa",
	"voltage",
];

export const SENSOR_UNITS: Readonly<Record<SensorKey, string>> = {
	rpm: "rpm",
	speedKph: "km/h",
	coolantC: "°C",
	throttlePercent: "%",
	mapKpa: "kPa",
	voltage: "V",
};

export interface SensorPayload {
	value: number | null;
	valid: boolean;
	status: SensorStatus;
	updatedAtMs: number | null;
	ageMs: number | null;
	source?: VoltageSource;
}

export interface SnapshotEnvelope {
	uptimeMs: number;
	btConnected: boolean;
	adapterReady: boolean;
	ecuResponding: boolean;
	obdConnected: boolean;
	rpm: SensorPayload;
	speedKph: SensorPayload;
	coolantC: SensorPayload;
	throttlePercent: SensorPayload;
	mapKpa: SensorPayload;
	voltage: SensorPayload;
}

export const OFFLINE_UPTIME_MS = 12_000;
export const CONNECTED_UPTIME_MS = 200_000;

/** Leitura bem-sucedida e recente, coerente com `uptimeMs`. */
export function ok(value: number, uptimeMs: number, ageMs = 1000): SensorPayload {
	return { value, valid: true, status: "ok", updatedAtMs: uptimeMs - ageMs, ageMs };
}

/** Nunca houve leitura bem-sucedida na sessão de boot. */
export function absent(status: SensorStatus = "unavailable"): SensorPayload {
	return { value: null, valid: false, status, updatedAtMs: null, ageMs: null };
}

/** PID confirmado como não suportado na sessão. */
export function notSupported(): SensorPayload {
	return absent("unsupported");
}

/** Última leitura venceu o prazo de frescor; o valor anterior é preservado. */
export function staleValue(value: number, uptimeMs: number, ageMs = 23_000): SensorPayload {
	return { value, valid: false, status: "stale", updatedAtMs: uptimeMs - ageMs, ageMs };
}

/** Última consulta falhou; valor anterior preservado quando existir. */
export function failedValue(
	value: number | null,
	status: "timeout" | "error",
	uptimeMs: number,
	ageMs = 23_000,
): SensorPayload {
	if (value === null) {
		return absent(status);
	}
	return { value, valid: false, status, updatedAtMs: uptimeMs - ageMs, ageMs };
}

/** Envelope de boot: nenhuma fase ativa e nenhuma leitura. */
export function offlineEnvelope(): SnapshotEnvelope {
	return {
		uptimeMs: OFFLINE_UPTIME_MS,
		btConnected: false,
		adapterReady: false,
		ecuResponding: false,
		obdConnected: false,
		rpm: absent(),
		speedKph: absent(),
		coolantC: absent(),
		throttlePercent: absent(),
		mapKpa: absent(),
		voltage: { ...absent(), source: "adapter" },
	};
}

/** Envelope com OBD conectado e leituras típicas do Onix. */
export function connectedEnvelope(overrides: Partial<SnapshotEnvelope> = {}): SnapshotEnvelope {
	const uptimeMs = CONNECTED_UPTIME_MS;
	const base: SnapshotEnvelope = {
		uptimeMs,
		btConnected: true,
		adapterReady: true,
		ecuResponding: true,
		obdConnected: true,
		rpm: ok(842, uptimeMs, 1000),
		speedKph: ok(0, uptimeMs, 1000),
		coolantC: ok(91, uptimeMs, 2000),
		throttlePercent: notSupported(),
		mapKpa: ok(31, uptimeMs, 500),
		voltage: { ...ok(14.1, uptimeMs, 1000), source: "adapter" },
	};
	return { ...base, ...overrides };
}

/** Mesmo estado, mas com a tensão vinda do PID 42 da ECU. */
export function withVoltageSource(
	envelope: SnapshotEnvelope,
	source: VoltageSource,
): SnapshotEnvelope {
	return { ...envelope, voltage: { ...envelope.voltage, source } };
}

/** Fases: só Bluetooth ativo. */
export function adapterStartingEnvelope(): SnapshotEnvelope {
	return {
		...offlineEnvelope(),
		uptimeMs: OFFLINE_UPTIME_MS,
		btConnected: true,
	};
}

/** Fases: Bluetooth e adaptador prontos, ECU ainda sem resposta recente. */
export function ecuSilentEnvelope(): SnapshotEnvelope {
	return { ...offlineEnvelope(), btConnected: true, adapterReady: true };
}

export function serialize(envelope: SnapshotEnvelope): string {
	return JSON.stringify(envelope);
}

/** Cópia mutável do envelope para montar payloads inválidos. */
export function draftOf(envelope: SnapshotEnvelope): SnapshotEnvelope & Record<string, unknown> {
	return JSON.parse(serialize(envelope)) as SnapshotEnvelope & Record<string, unknown>;
}

export function serializeDraft(draft: SnapshotEnvelope & Record<string, unknown>): string {
	return JSON.stringify(draft);
}

/** Substituição única e verificada de um trecho do corpo (valores não finitos, tipos errados). */
export function replaceOnce(body: string, from: string, to: string): string {
	if (!body.includes(from)) {
		throw new Error(`trecho ausente no corpo de teste: ${from}`);
	}
	return body.replace(from, to);
}
