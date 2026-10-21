// logs.ts — contrato, parsing e formatação de GET /api/logs (diagnóstico).
// Sem I/O: testável. Caminho relativo, na mesma origem da página e de /api/data.

export const LOGS_PATH = "/api/logs";

/** Dump cru do histórico persistido (ADR-0018): text/plain com Content-Disposition: attachment. */
export const LOGS_DOWNLOAD_PATH = "/api/logs/download";

export const LOG_LEVELS = ["error", "warn", "info", "debug"] as const;

export type LogLevel = (typeof LOG_LEVELS)[number];

export interface LogEntry {
	uptimeMs: number;
	level: LogLevel;
	tag: string;
	message: string;
}

/** Entrada do histórico persistido no LittleFS: sem uptime, o carimbo está no próprio texto. */
export interface LogHistoryEntry {
	level: LogLevel;
	tag: string;
	message: string;
	/** Carimbo ISO 8601 (UTC) ou "----------------Z" sem relógio; ausente em firmware anterior. */
	ts?: string;
}

export interface LogsSnapshot {
	uptimeMs: number;
	/** Entradas perdidas por overflow do buffer circular no firmware. */
	dropped: number;
	/** Da mais antiga para a mais recente. */
	entries: LogEntry[];
	/** Linhas warn+ perdidas por falta de espaço no arquivo (ADR-0018; 0 sem persistência). */
	droppedLack: number;
	/** Bytes gravados no histórico persistido (0 sem persistência). */
	persisted: number;
	/** Histórico do arquivo, da mais antiga para a mais recente (vazio sem persistência). */
	history: LogHistoryEntry[];
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

function parseHistoryEntry(raw: unknown): LogHistoryEntry | null {
	if (typeof raw !== "object" || raw === null) return null;
	const record = raw as Record<string, unknown>;
	const { level, tag, message } = record;
	if (!isLogLevel(level)) return null;
	if (typeof tag !== "string" || typeof message !== "string") return null;
	const entry: LogHistoryEntry = { level, tag, message };
	if (typeof record.ts === "string") entry.ts = record.ts;
	return entry;
}

/** Indica se o carimbo do histórico é exibível: não vazio e com algum dígito (não "----…Z"). */
export function hasHistoryTimestamp(ts: string | undefined): ts is string {
	return ts !== undefined && /[0-9]/.test(ts);
}

/** Campos do histórico persistido são opcionais: firmware antigo (sem ADR-0018) não os envia. */
function parseOptionalHistory(record: Record<string, unknown>): {
	droppedLack: number;
	persisted: number;
	history: LogHistoryEntry[];
} | null {
	const droppedLack = record.droppedLack ?? 0;
	const persisted = record.persisted ?? 0;
	if (!isNonNegativeNumber(droppedLack) || !Number.isInteger(droppedLack))
		return null;
	if (!isNonNegativeNumber(persisted) || !Number.isInteger(persisted))
		return null;
	const rawHistory = record.history ?? [];
	if (!Array.isArray(rawHistory)) return null;
	const history: LogHistoryEntry[] = [];
	for (const raw of rawHistory) {
		const entry = parseHistoryEntry(raw);
		if (entry === null) return null;
		history.push(entry);
	}
	return { droppedLack, persisted, history };
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
	const persisted = parseOptionalHistory(record);
	if (persisted === null) return null;
	return { uptimeMs, dropped, entries: parsed, ...persisted };
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

/** Formata bytes do histórico em B/KiB (ex.: 512 B, 1,5 KiB). */
export function formatBytes(bytes: number): string {
	const safe = Number.isFinite(bytes) && bytes > 0 ? bytes : 0;
	if (safe < 1024) {
		return `${Math.round(safe)} B`;
	}
	const kib = safe / 1024;
	return `${kib.toFixed(1).replace(".", ",")} KiB`;
}
