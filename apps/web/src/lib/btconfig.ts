// btconfig.ts — contrato, validação e mensagens de /api/obd/config e /api/obd/scan (ADR-0014).
// Sem I/O: testável. Caminhos relativos, na mesma origem da página.

export const BT_CONFIG_PATH = "/api/obd/config";
export const BT_SCAN_PATH = "/api/obd/scan";

export const BT_SCAN_STATES = [
	"idle",
	"pending",
	"scanning",
	"done",
	"busy",
	"failed",
] as const;

export type BtScanState = (typeof BT_SCAN_STATES)[number];

export interface BtDevice {
	/** Vazio quando o dispositivo não anunciou nome na busca. */
	name: string;
	mac: string;
	rssi: number | null;
}

export interface BtConfigStatus {
	/** true = configuração gravada pelo painel; false = padrões do firmware. */
	custom: boolean;
	name: string;
	pin: string;
	mac: string;
	scan: { state: BtScanState; devices: BtDevice[] };
}

export interface BtConfigInput {
	name: string;
	pin: string;
	mac: string;
}

export const BT_NAME_MAX_BYTES = 31;
export const BT_PIN_MAX_BYTES = 16;
const MAC_PATTERN = /^[0-9A-Fa-f]{2}([:-][0-9A-Fa-f]{2}){5}$/;

function isScanState(value: unknown): value is BtScanState {
	return (
		typeof value === "string" &&
		(BT_SCAN_STATES as readonly string[]).includes(value)
	);
}

function parseDevice(json: unknown): BtDevice | null {
	if (typeof json !== "object" || json === null) return null;
	const { name, mac, rssi } = json as Record<string, unknown>;
	if (typeof name !== "string" || typeof mac !== "string") return null;
	if (rssi !== null && typeof rssi !== "number") return null;
	return { name, mac, rssi };
}

/** Valida um valor já decodificado contra o contrato de GET /api/obd/config. */
export function parseBtConfigStatus(json: unknown): BtConfigStatus | null {
	if (typeof json !== "object" || json === null) return null;
	const { custom, name, pin, mac, scan } = json as Record<string, unknown>;
	if (typeof custom !== "boolean") return null;
	if (
		typeof name !== "string" ||
		typeof pin !== "string" ||
		typeof mac !== "string"
	) {
		return null;
	}
	if (typeof scan !== "object" || scan === null) return null;
	const { state, devices } = scan as Record<string, unknown>;
	if (!isScanState(state) || !Array.isArray(devices)) return null;
	const parsedDevices: BtDevice[] = [];
	for (const device of devices) {
		const parsed = parseDevice(device);
		if (parsed === null) return null;
		parsedDevices.push(parsed);
	}
	return { custom, name, pin, mac, scan: { state, devices: parsedDevices } };
}

function utf8Length(text: string): number {
	return new TextEncoder().encode(text).length;
}

/** "aa-bb-..." -> "AA:BB:..."; texto vazio continua vazio. */
export function normalizeMac(mac: string): string {
	return mac.trim().toUpperCase().replaceAll("-", ":");
}

/** Valida o formulário antes do envio. Retorna a mensagem de erro ou null. */
export function validateBtConfig({
	name,
	pin,
	mac,
}: BtConfigInput): string | null {
	const cleanMac = mac.trim();
	if (name.trim() === "" && cleanMac === "") {
		return "Informe o nome do scanner ou o MAC.";
	}
	if (utf8Length(name) > BT_NAME_MAX_BYTES) {
		return `O nome deve ter no máximo ${BT_NAME_MAX_BYTES} bytes.`;
	}
	if (utf8Length(pin) > BT_PIN_MAX_BYTES) {
		return `O PIN deve ter no máximo ${BT_PIN_MAX_BYTES} caracteres.`;
	}
	if (cleanMac !== "" && !MAC_PATTERN.test(cleanMac)) {
		return "MAC inválido. Use o formato AA:BB:CC:DD:EE:FF.";
	}
	return null;
}

/** Busca ainda em andamento: o painel continua consultando o estado. */
export function isScanRunning(state: BtScanState): boolean {
	return state === "pending" || state === "scanning";
}

function errorCodeOf(body: unknown): string | null {
	if (typeof body !== "object" || body === null) return null;
	const { error } = body as Record<string, unknown>;
	return typeof error === "string" ? error : null;
}

/** Mensagem em português para uma resposta de erro de /api/obd/config ou /api/obd/scan. */
export function describeBtError(status: number, body: unknown): string {
	if (status === 404) {
		return "Configuração do Bluetooth indisponível neste firmware.";
	}
	switch (errorCodeOf(body)) {
		case "invalid-json":
			return "A placa não entendeu a requisição. Recarregue a página e tente de novo.";
		case "invalid-config":
			return "Nome, PIN ou MAC inválidos.";
		case "storage":
			return "A placa não conseguiu gravar a configuração. Tente de novo.";
		case "busy":
			return "Já há uma busca em andamento.";
		default:
			return `Falha na requisição (HTTP ${status}).`;
	}
}

export function scanStateLabel(state: BtScanState): string {
	switch (state) {
		case "idle":
			return "";
		case "pending":
		case "scanning":
			return "Buscando dispositivos (cerca de 5 s)…";
		case "done":
			return "Busca concluída.";
		case "busy":
			return "O scanner está conectado: a busca só funciona desconectada.";
		case "failed":
			return "A placa não conseguiu buscar. Tente de novo.";
	}
}
