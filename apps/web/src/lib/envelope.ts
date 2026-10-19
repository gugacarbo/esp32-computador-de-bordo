// envelope.ts — parsing e validação do envelope JSON de GET /api/data.
// Espelha serializeSnapshotJson (apps/firmware/src/api.cpp) e a validação
// do painel legado (apps/firmware/include/dashboard.h). Sem I/O: testável.

export const SENSOR_KEYS = [
	"rpm",
	"speedKph",
	"coolantC",
	"throttlePercent",
	"mapKpa",
	"voltage",
] as const;

export type SensorKey = (typeof SENSOR_KEYS)[number];

export const SENSOR_STATUSES = [
	"unavailable",
	"unsupported",
	"timeout",
	"error",
	"stale",
	"ok",
] as const;

export type SensorStatus = (typeof SENSOR_STATUSES)[number];

export type VoltageSource = "adapter" | "ecu";

export interface SensorReading {
	value: number | null;
	valid: boolean;
	status: SensorStatus;
	updatedAtMs: number | null;
	ageMs: number | null;
}

export interface VoltageReading extends SensorReading {
	source: VoltageSource;
}

export interface TelemetryEnvelope {
	uptimeMs: number;
	btConnected: boolean;
	adapterReady: boolean;
	ecuResponding: boolean;
	obdConnected: boolean;
	/** A placa desistiu de buscar o scanner BT após várias tentativas. */
	btSearchStopped: boolean;
	rpm: SensorReading;
	speedKph: SensorReading;
	coolantC: SensorReading;
	throttlePercent: SensorReading;
	mapKpa: SensorReading;
	voltage: VoltageReading;
}

export type PanelState =
	| "loading"
	| "disconnected"
	| "adapter"
	| "ecu"
	| "connected"
	| "comm-error";

export const PANEL_TEXT: Record<PanelState, string> = {
	loading: "Carregando dados",
	disconnected: "OBD desconectado",
	adapter: "Inicializando adaptador",
	ecu: "ECU sem resposta",
	connected: "OBD conectado",
	"comm-error": "Sem comunicação com o ESP32",
};

export const SENSOR_UNITS: Record<SensorKey, string> = {
	rpm: "rpm",
	speedKph: "km/h",
	coolantC: "°C",
	throttlePercent: "%",
	mapKpa: "kPa",
	voltage: "V",
};

function isFiniteNumber(value: unknown): value is number {
	return typeof value === "number" && Number.isFinite(value);
}

function isSensorStatus(value: unknown): value is SensorStatus {
	return (
		typeof value === "string" &&
		(SENSOR_STATUSES as readonly string[]).includes(value)
	);
}

function parseSensorReading(raw: unknown): SensorReading | null {
	if (typeof raw !== "object" || raw === null) return null;
	const record = raw as Record<string, unknown>;
	const { value, valid, status, updatedAtMs, ageMs } = record;
	if (value !== null && !isFiniteNumber(value)) return null;
	if (typeof valid !== "boolean" || !isSensorStatus(status)) return null;
	if (valid !== (status === "ok")) return null;
	if (updatedAtMs !== null && typeof updatedAtMs !== "number") return null;
	if (ageMs !== null && typeof ageMs !== "number") return null;
	return { value, valid, status, updatedAtMs, ageMs };
}

/** Valida um valor já decodificado contra o contrato do envelope. */
export function asEnvelope(data: unknown): TelemetryEnvelope | null {
	if (typeof data !== "object" || data === null) return null;
	const record = data as Record<string, unknown>;
	const uptimeMs = record["uptimeMs"];
	if (typeof uptimeMs !== "number") return null;
	for (const flag of [
		"btConnected",
		"adapterReady",
		"ecuResponding",
		"obdConnected",
	]) {
		if (typeof record[flag] !== "boolean") return null;
	}
	// Campo ausente vem de firmware antigo: tratado como busca não parada.
	const searchStoppedRaw = record["btSearchStopped"];
	const searchStopped =
		searchStoppedRaw === undefined ? false : searchStoppedRaw;
	if (typeof searchStopped !== "boolean") return null;
	const readings: Record<SensorKey, SensorReading> = {} as Record<
		SensorKey,
		SensorReading
	>;
	for (const key of SENSOR_KEYS) {
		const reading = parseSensorReading(record[key]);
		if (reading === null) return null;
		readings[key] = reading;
	}
	const voltageRaw = record["voltage"];
	if (typeof voltageRaw !== "object" || voltageRaw === null) return null;
	const source = (voltageRaw as Record<string, unknown>)["source"];
	if (source !== "adapter" && source !== "ecu") return null;
	return {
		uptimeMs,
		btConnected: record["btConnected"] as boolean,
		adapterReady: record["adapterReady"] as boolean,
		ecuResponding: record["ecuResponding"] as boolean,
		obdConnected: record["obdConnected"] as boolean,
		btSearchStopped: searchStopped,
		rpm: readings["rpm"],
		speedKph: readings["speedKph"],
		coolantC: readings["coolantC"],
		throttlePercent: readings["throttlePercent"],
		mapKpa: readings["mapKpa"],
		voltage: { ...readings["voltage"], source },
	};
}

/** Decodifica o corpo textual (WS ou fetch) para envelope válido ou null. */
export function parseEnvelopeText(text: string): TelemetryEnvelope | null {
	let data: unknown;
	try {
		data = JSON.parse(text);
	} catch {
		return null;
	}
	return asEnvelope(data);
}

export interface SessionFlags {
	btConnected: boolean;
	adapterReady: boolean;
	ecuResponding: boolean;
}

/** Deriva o estado do painel a partir das fases da sessão (igual ao legado). */
export function derivePanelState(
	flags: SessionFlags,
): Exclude<PanelState, "loading" | "comm-error"> {
	if (!flags.btConnected) return "disconnected";
	if (!flags.adapterReady) return "adapter";
	if (!flags.ecuResponding) return "ecu";
	return "connected";
}

/** Formatação por sensor: tensão com 1 casa, demais arredondados (dashboard.h). */
export function formatSensorValue(key: SensorKey, value: number): string {
	if (key === "voltage") return value.toFixed(1);
	return String(Math.round(value));
}

export type StatusTone = "ok" | "warn" | "error" | "muted";

export interface StatusInfo {
	label: string;
	tone: StatusTone;
}

/** Texto pt-BR do estado de um sensor (igual ao renderSensor legado). */
export function sensorStatusInfo(reading: SensorReading): StatusInfo {
	if (reading.valid && reading.status === "ok")
		return { label: "OK", tone: "ok" };
	if (reading.status === "unsupported")
		return { label: "Não suportado", tone: "muted" };
	if (reading.status === "stale")
		return { label: "Desatualizado", tone: "warn" };
	if (reading.status === "timeout" || reading.status === "error")
		return { label: "Falha na leitura", tone: "error" };
	return { label: "Sem leitura", tone: "muted" };
}

export function voltageTitle(source: VoltageSource): string {
	return source === "ecu" ? "Tensão da ECU" : "Tensão do adaptador";
}
