// logs.ts — contrato, parsing e formatação de GET /api/logs (diagnóstico).
// Sem I/O: testável. Caminho relativo, na mesma origem da página e de /api/data.

export const LOGS_PATH = "/api/logs";

export const LOG_LEVELS = ["error", "warn", "info", "debug"] as const;

export type LogLevel = (typeof LOG_LEVELS)[number];

export interface LogEntry {
	uptimeMs: number;
	level: LogLevel;
	tag: string;
	message: string;
}

export interface LogsSnapshot {
	uptimeMs: number;
	/** Entradas perdidas por overflow do buffer circular no firmware. */
	dropped: number;
	/** Da mais antiga para a mais recente. */
	entries: LogEntry[];
}

function isNonNegativeNumber(value: unknown): value is number {
	return typeof value === "number" && Number.isFinite(value) && value >= 0;
}

function isLogLevel(value: unknown): value is LogLevel {
	return (
		typeof value === "string" &&
		(LOG_LEVELS as readonly string[]).includes(value)
	);
}

function parseLogEntry(raw: unknown): LogEntry | null {
	if (typeof raw !== "object" || raw === null) return null;
	const record = raw as Record<string, unknown>;
	const { uptimeMs, level, tag, message } = record;
	if (!isNonNegativeNumber(uptimeMs) || !isLogLevel(level)) return null;
	if (typeof tag !== "string" || typeof message !== "string") return null;
	return { uptimeMs, level, tag, message };
}

/** Valida um valor já decodificado contra o contrato de /api/logs. */
export function parseLogsResponse(json: unknown): LogsSnapshot | null {
	if (typeof json !== "object" || json === null) return null;
	const record = json as Record<string, unknown>;
	const { uptimeMs, dropped, entries } = record;
	if (!isNonNegativeNumber(uptimeMs)) return null;
	if (!isNonNegativeNumber(dropped) || !Number.isInteger(dropped)) return null;
	if (!Array.isArray(entries)) return null;
	const parsed: LogEntry[] = [];
	for (const raw of entries) {
		const entry = parseLogEntry(raw);
		if (entry === null) return null;
		parsed.push(entry);
	}
	return { uptimeMs, dropped, entries: parsed };
}

/** Decodifica o corpo textual de /api/logs para snapshot válido ou null. */
export function parseLogsText(text: string): LogsSnapshot | null {
	let data: unknown;
	try {
		data = JSON.parse(text);
	} catch {
		return null;
	}
	return parseLogsResponse(data);
}

function pad2(value: number): string {
	return value < 10 ? `0${value}` : String(value);
}

/**
 * Formata uptime em ms: `mm:ss.d` até 1 hora (ex.: 12:34.5),
 * `Hh mm:ss` a partir daí (ex.: 1h02:03).
 */
export function formatUptime(ms: number): string {
	const safe = Number.isFinite(ms) && ms > 0 ? ms : 0;
	const totalTenths = Math.floor(safe / 100);
	const tenths = totalTenths % 10;
	const totalSeconds = Math.floor(totalTenths / 10);
	const seconds = totalSeconds % 60;
	const minutes = Math.floor(totalSeconds / 60) % 60;
	const hours = Math.floor(totalSeconds / 3600);
	if (hours > 0) {
		return `${hours}h${pad2(minutes)}:${pad2(seconds)}`;
	}
	return `${pad2(minutes)}:${pad2(seconds)}.${tenths}`;
}
