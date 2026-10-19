// obd.ts — pedido de nova busca pelo scanner OBD Bluetooth (POST /api/obd/reconnect).
// Caminho relativo, na mesma origem da página. Sem estado: testável com fetch falso.

export const OBD_RECONNECT_PATH = "/api/obd/reconnect";

const RECONNECT_TIMEOUT_MS = 3000;

export interface ObdReconnectResult {
	ok: boolean;
	error?: string;
}

/**
 * Pede à placa para voltar a procurar o scanner. 202 é aceito; qualquer outra
 * resposta (503 indisponível, 405, erro de rede ou timeout) vira mensagem em pt-BR.
 */
export async function requestObdReconnect(
	fetchImpl: typeof fetch = fetch,
): Promise<ObdReconnectResult> {
	const ctrl = new AbortController();
	const timeout = setTimeout(() => ctrl.abort(), RECONNECT_TIMEOUT_MS);
	try {
		const response = await fetchImpl(OBD_RECONNECT_PATH, {
			method: "POST",
			cache: "no-store",
			signal: ctrl.signal,
		});
		if (response.status === 202) return { ok: true };
		if (response.status === 503) {
			return {
				ok: false,
				error: "Scanner indisponível agora. Tente de novo em instantes.",
			};
		}
		return {
			ok: false,
			error: `Falha ao pedir a busca (HTTP ${response.status}).`,
		};
	} catch {
		return {
			ok: false,
			error: "Sem resposta da placa. Tente de novo.",
		};
	} finally {
		clearTimeout(timeout);
	}
}
