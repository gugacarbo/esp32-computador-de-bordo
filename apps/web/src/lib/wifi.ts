// wifi.ts — contrato, validação e mensagens de GET/POST/DELETE /api/wifi.
// Sem I/O: testável. Caminho relativo, na mesma origem da página.

export const WIFI_PATH = "/api/wifi";

export const WIFI_MODES = [
	"connecting",
	"station",
	"station-lost",
	"access-point",
] as const;

export type WifiMode = (typeof WIFI_MODES)[number];

export interface WifiStatus {
	mode: WifiMode;
	/** Há um hotspot cadastrado na placa. */
	configured: boolean;
	/** SSID cadastrado (vazio quando não há configuração). */
	ssid: string;
	/** IP atual da placa (vazio quando não há conexão). */
	ip: string;
}

export interface WifiFormInput {
	ssid: string;
	password: string;
}

export const SSID_MAX_BYTES = 32;
export const PASSWORD_MIN_BYTES = 8;
export const PASSWORD_MAX_BYTES = 63;

function isWifiMode(value: unknown): value is WifiMode {
	return (
		typeof value === "string" &&
		(WIFI_MODES as readonly string[]).includes(value)
	);
}

/** Valida um valor já decodificado contra o contrato de GET /api/wifi. */
export function parseWifiStatus(json: unknown): WifiStatus | null {
	if (typeof json !== "object" || json === null) return null;
	const record = json as Record<string, unknown>;
	const { mode, configured, ssid, ip } = record;
	if (!isWifiMode(mode) || typeof configured !== "boolean") return null;
	if (typeof ssid !== "string" || typeof ip !== "string") return null;
	return { mode, configured, ssid, ip };
}

/** Valida a resposta de sucesso de POST/DELETE /api/wifi (`{ok:true,restarting:true}`). */
export function parseWifiActionResponse(
	json: unknown,
): { ok: true; restarting: boolean } | null {
	if (typeof json !== "object" || json === null) return null;
	const record = json as Record<string, unknown>;
	if (record.ok !== true || typeof record.restarting !== "boolean") return null;
	return { ok: true, restarting: record.restarting };
}

/** Tamanho em bytes UTF-8 (o limite do SSID e da senha é em bytes, não em caracteres). */
export function utf8Length(text: string): number {
	return new TextEncoder().encode(text).length;
}

/** Valida o formulário antes do envio. Retorna a mensagem de erro ou null. */
export function validateWifiForm({
	ssid,
	password,
}: WifiFormInput): string | null {
	const ssidBytes = utf8Length(ssid);
	if (ssidBytes === 0) return "Informe o nome (SSID) do hotspot.";
	if (ssidBytes > SSID_MAX_BYTES) {
		return `O nome do hotspot deve ter no máximo ${SSID_MAX_BYTES} bytes em UTF-8 (acentos ocupam mais de um byte).`;
	}
	if (password.length > 0) {
		const passwordBytes = utf8Length(password);
		if (
			passwordBytes < PASSWORD_MIN_BYTES ||
			passwordBytes > PASSWORD_MAX_BYTES
		) {
			return `A senha do hotspot deve ter de ${PASSWORD_MIN_BYTES} a ${PASSWORD_MAX_BYTES} bytes, ou ficar vazia.`;
		}
	}
	return null;
}

export function wifiModeLabel(mode: WifiMode): string {
	switch (mode) {
		case "connecting":
			return "Conectando ao hotspot";
		case "station":
			return "Conectado ao hotspot";
		case "station-lost":
			return "Hotspot perdido, reconectando";
		case "access-point":
			return "Rede própria (Onix-MVP)";
	}
}

function errorCodeOf(body: unknown): string | null {
	if (typeof body !== "object" || body === null) return null;
	const { error } = body as Record<string, unknown>;
	return typeof error === "string" ? error : null;
}

/** Mensagem em português para uma resposta de erro de /api/wifi. */
export function describeWifiError(status: number, body: unknown): string {
	if (status === 404) {
		return "Configuração Wi-Fi indisponível neste firmware.";
	}
	if (status === 403) return "Senha do OTA incorreta";
	switch (errorCodeOf(body)) {
		case "invalid-json":
			return "A placa não entendeu a requisição. Recarregue a página e tente de novo.";
		case "invalid-credentials":
			return "Nome ou senha do hotspot inválidos. Confira os limites de bytes.";
		case "storage":
			return "A placa não conseguiu gravar a configuração. Tente de novo.";
		default:
			return `Falha na requisição (HTTP ${status}).`;
	}
}
