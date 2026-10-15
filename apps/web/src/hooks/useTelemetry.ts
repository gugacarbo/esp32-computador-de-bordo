import { useEffect, useState } from "react";
import {
	derivePanelState,
	type PanelState,
	parseEnvelopeText,
	type TelemetryEnvelope,
} from "../lib/envelope";
import {
	API_PATH,
	backoffDelayMs,
	buildWsUrl,
	resolveHost,
} from "../lib/source";

const POLL_MS = 500;
const FETCH_TIMEOUT_MS = 4000;
const WS_STALE_MS = 4000;

export type Transport = "ws" | "polling";

export interface TelemetrySnapshot {
	envelope: TelemetryEnvelope | null;
	panel: PanelState;
	transport: Transport;
	wsOnline: boolean;
}

interface LoopBox {
	envelope: TelemetryEnvelope | null;
	wsOnline: boolean;
	lastMessageAt: number;
	attempts: number;
	ws: WebSocket | null;
	reconnectTimer: number;
	pollTimer: number;
	inFlight: boolean;
}

/**
 * Telemetria via WebSocket (ws://<host>:81/ws, mesmo envelope em texto)
 * com fallback para polling de GET /api/data a cada ~500ms (máx. 1
 * requisição em voo) enquanto o WS estiver indisponível.
 */
export function useTelemetry(): TelemetrySnapshot {
	const [envelope, setEnvelope] = useState<TelemetryEnvelope | null>(null);
	const [panel, setPanel] = useState<PanelState>("loading");
	const [transport, setTransport] = useState<Transport>("polling");
	const [wsOnline, setWsOnline] = useState(false);

	useEffect(() => {
		let disposed = false;
		const box: LoopBox = {
			envelope: null,
			wsOnline: false,
			lastMessageAt: 0,
			attempts: 0,
			ws: null,
			reconnectTimer: 0,
			pollTimer: 0,
			inFlight: false,
		};

		const applyEnvelope = (next: TelemetryEnvelope, via: Transport): void => {
			box.envelope = next;
			box.lastMessageAt = Date.now();
			setEnvelope(next);
			setPanel(derivePanelState(next));
			setTransport(via);
		};

		const applyCommFailure = (): void => {
			setPanel("comm-error");
			setTransport(box.wsOnline ? "ws" : "polling");
		};

		const pollOnce = async (): Promise<void> => {
			if (disposed || box.inFlight || document.visibilityState === "hidden")
				return;
			if (box.wsOnline) {
				if (
					box.lastMessageAt > 0 &&
					Date.now() - box.lastMessageAt > WS_STALE_MS
				) {
					applyCommFailure();
				}
				return;
			}
			box.inFlight = true;
			const ctrl = new AbortController();
			const timeout = window.setTimeout(() => ctrl.abort(), FETCH_TIMEOUT_MS);
			try {
				const response = await fetch(API_PATH, {
					cache: "no-store",
					signal: ctrl.signal,
				});
				if (!response.ok) throw new Error(`http ${response.status}`);
				const parsed = parseEnvelopeText(await response.text());
				if (parsed === null) throw new Error("shape");
				if (!disposed) applyEnvelope(parsed, "polling");
			} catch {
				if (!disposed) applyCommFailure();
			} finally {
				window.clearTimeout(timeout);
				box.inFlight = false;
			}
		};

		const scheduleReconnect = (): void => {
			if (disposed) return;
			const delay = backoffDelayMs(box.attempts);
			box.attempts += 1;
			window.clearTimeout(box.reconnectTimer);
			box.reconnectTimer = window.setTimeout(() => {
				if (!disposed) connect();
			}, delay);
		};

		const connect = (): void => {
			if (disposed) return;
			let ws: WebSocket;
			try {
				ws = new WebSocket(buildWsUrl(resolveHost()));
			} catch {
				scheduleReconnect();
				return;
			}
			box.ws = ws;
			ws.onopen = () => {
				if (disposed) {
					ws.close();
					return;
				}
				box.wsOnline = true;
				box.attempts = 0;
				box.lastMessageAt = Date.now();
				setWsOnline(true);
				setTransport("ws");
			};
			ws.onmessage = (event: MessageEvent) => {
				if (disposed || typeof event.data !== "string") return;
				const parsed = parseEnvelopeText(event.data);
				if (parsed !== null) applyEnvelope(parsed, "ws");
			};
			ws.onclose = () => {
				box.wsOnline = false;
				setWsOnline(false);
				if (disposed) return;
				if (box.envelope === null) applyCommFailure();
				scheduleReconnect();
			};
			ws.onerror = () => {
				ws.close();
			};
		};

		connect();
		void pollOnce();
		box.pollTimer = window.setInterval(() => {
			void pollOnce();
		}, POLL_MS);

		return () => {
			disposed = true;
			window.clearTimeout(box.reconnectTimer);
			window.clearInterval(box.pollTimer);
			if (box.ws !== null) {
				box.ws.onclose = null;
				box.ws.close();
			}
		};
	}, []);

	return { envelope, panel, transport, wsOnline };
}
