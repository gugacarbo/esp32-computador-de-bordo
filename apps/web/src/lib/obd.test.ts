import { describe, expect, test } from "bun:test";
import { OBD_RECONNECT_PATH, requestObdReconnect } from "./obd";

type FetchCall = { url: string; init: RequestInit | undefined };

function fakeFetch(
	respond: () => Promise<Response>,
	calls: FetchCall[] = [],
): typeof fetch {
	return ((url: string | URL | Request, init?: RequestInit) => {
		calls.push({ url: String(url), init });
		return respond();
	}) as typeof fetch;
}

describe("requestObdReconnect", () => {
	test("202 é aceito e usa POST no caminho relativo", async () => {
		const calls: FetchCall[] = [];
		const result = await requestObdReconnect(
			fakeFetch(async () => new Response(null, { status: 202 }), calls),
		);
		expect(result).toEqual({ ok: true });
		expect(calls).toHaveLength(1);
		expect(calls[0]?.url).toBe(OBD_RECONNECT_PATH);
		expect(calls[0]?.init?.method).toBe("POST");
	});

	test("503 vira erro de indisponibilidade", async () => {
		const result = await requestObdReconnect(
			fakeFetch(
				async () =>
					new Response(JSON.stringify({ ok: false }), { status: 503 }),
			),
		);
		expect(result.ok).toBe(false);
		expect(result.error).toContain("indisponível");
	});

	test("outro status (ex.: 405) vira erro com o código", async () => {
		const result = await requestObdReconnect(
			fakeFetch(async () => new Response(null, { status: 405 })),
		);
		expect(result.ok).toBe(false);
		expect(result.error).toContain("405");
	});

	test("falha de rede vira erro sem lançar", async () => {
		const result = await requestObdReconnect(
			fakeFetch(async () => {
				throw new TypeError("Failed to fetch");
			}),
		);
		expect(result).toEqual({
			ok: false,
			error: "Sem resposta da placa. Tente de novo.",
		});
	});

	test("timeout aborta a requisição e vira erro", async () => {
		const hangingFetch = ((_url: string | URL | Request, init?: RequestInit) =>
			new Promise<Response>((_resolve, reject) => {
				// Rede travada: a promessa só rejeita quando o sinal é abortado.
				init?.signal?.addEventListener("abort", () =>
					reject(new DOMException("aborted", "AbortError")),
				);
			})) as typeof fetch;
		const result = await requestObdReconnect(hangingFetch);
		expect(result).toEqual({
			ok: false,
			error: "Sem resposta da placa. Tente de novo.",
		});
	}, 8000);
});
