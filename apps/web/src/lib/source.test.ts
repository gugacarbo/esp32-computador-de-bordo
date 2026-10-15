import { describe, expect, test } from "bun:test";
import {
	API_PATH,
	backoffDelayMs,
	buildWsUrl,
	WS_PATH,
	WS_PORT,
} from "./source";

describe("buildWsUrl", () => {
	test("monta ws://<host>:81/ws", () => {
		expect(buildWsUrl("192.168.4.1")).toBe("ws://192.168.4.1:81/ws");
		expect(buildWsUrl("esp32.local")).toBe("ws://esp32.local:81/ws");
	});

	test("host vazio cai para localhost", () => {
		expect(buildWsUrl("")).toBe("ws://localhost:81/ws");
		expect(buildWsUrl("   ")).toBe("ws://localhost:81/ws");
	});

	test("constantes do protocolo", () => {
		expect(WS_PORT).toBe(81);
		expect(WS_PATH).toBe("/ws");
		expect(API_PATH).toBe("/api/data");
	});
});

describe("backoffDelayMs", () => {
	test("começa em ~1s e limita em 3s", () => {
		expect(backoffDelayMs(0)).toBe(1000);
		expect(backoffDelayMs(1)).toBe(1500);
		expect(backoffDelayMs(2)).toBe(2250);
		expect(backoffDelayMs(10)).toBe(3000);
		expect(backoffDelayMs(-5)).toBe(1000);
	});
});
