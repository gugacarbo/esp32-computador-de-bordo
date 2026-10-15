// source.ts — construção das URLs de telemetria (WS + HTTP fallback).

export const WS_PORT = 81;
export const WS_PATH = "/ws";
export const API_PATH = "/api/data";

/** ws://<host>:81/ws — protocolo assumido do painel do firmware. */
export function buildWsUrl(host: string): string {
	const clean = host.trim() === "" ? "localhost" : host.trim();
	return `ws://${clean}:${WS_PORT}${WS_PATH}`;
}

/** Host de origem da página servida (em produção, o próprio ESP32). */
export function resolveHost(): string {
	if (typeof window !== "undefined" && window.location?.hostname !== "") {
		return window.location.hostname;
	}
	return "localhost";
}

/** Backoff de reconexão do WS: ~1s crescendo até o teto de 3s. */
export function backoffDelayMs(attempt: number): number {
	return Math.min(1000 * 1.5 ** Math.max(0, attempt), 3000);
}
