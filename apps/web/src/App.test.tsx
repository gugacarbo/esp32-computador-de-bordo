import { describe, expect, test } from "bun:test";
import { createElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { AppView } from "./App";
import type { Transport } from "./hooks/useTelemetry";
import type {
	PanelState,
	SensorReading,
	TelemetryEnvelope,
} from "./lib/envelope";
import { DEFAULT_TAB_ID, TAB_LABELS, TABS, type TabId } from "./lib/tabs";

const okReading = (value: number): SensorReading => ({
	value,
	valid: true,
	status: "ok",
	updatedAtMs: 15,
	ageMs: 40,
});

function makeEnvelope(
	overrides: Partial<TelemetryEnvelope> = {},
): TelemetryEnvelope {
	return {
		uptimeMs: 65_000,
		btConnected: true,
		adapterReady: true,
		ecuResponding: true,
		obdConnected: true,
		btSearchStopped: false,
		rpm: okReading(2450),
		speedKph: okReading(87),
		coolantC: okReading(91),
		throttlePercent: okReading(18),
		mapKpa: okReading(35),
		voltage: { ...okReading(13.9), source: "adapter" },
		...overrides,
	};
}

function renderTab(tab: TabId, envelope: TelemetryEnvelope | null): string {
	return renderToStaticMarkup(
		createElement(AppView, {
			tab,
			envelope,
			panel: (envelope === null ? "loading" : "connected") as PanelState,
			transport: "ws" as Transport,
			wsOnline: true,
		}),
	);
}

describe("AppView (SSR)", () => {
	test("aba padrão é Telemetria", () => {
		expect(DEFAULT_TAB_ID).toBe("telemetria");
	});

	test("barra de abas lista as três abas com acordeão correto", () => {
		const markup = renderTab("telemetria", makeEnvelope());
		for (const id of TABS) {
			expect(markup).toContain(TAB_LABELS[id]);
		}
	});

	test("estado do OBD fica visível junto à telemetria", () => {
		const markup = renderTab("telemetria", makeEnvelope());
		expect(markup).toContain('id="panel-status"');
		expect(markup).toContain("OBD conectado");
	});

	test("Telemetria mostra hero de velocidade e RPM, sem seções de config", () => {
		const markup = renderTab("telemetria", makeEnvelope());
		expect(markup).toContain('id="value-speedKph"');
		expect(markup).toContain('id="value-rpm"');
		expect(markup).toContain('id="card-coolantC"');
		expect(markup).toContain('id="card-mapKpa"');
		expect(markup).toContain('id="card-throttlePercent"');
		expect(markup).toContain('id="value-voltage"');
		expect(markup).not.toContain('id="bluetooth"');
		expect(markup).not.toContain('id="wifi"');
		expect(markup).not.toContain('id="diagnostics"');
	});

	test("Telemetria sem envelope mostra traço, mas mantém leituras anteriores", () => {
		const markup = renderTab("telemetria", null);
		expect(markup).toContain('id="panel-status"');
		expect(markup).toContain("—");
	});

	test("Conexão mostra o Bluetooth e o aviso de busca pausada, sem Wi-Fi nem logs", () => {
		const markup = renderTab("conexao", makeEnvelope());
		expect(markup).toContain('id="bluetooth"');
		expect(markup).not.toContain('id="wifi"');
		expect(markup).not.toContain('id="diagnostics"');
	});

	test("Conexão mantém o estado do OBD visível", () => {
		const markup = renderTab("conexao", makeEnvelope());
		expect(markup).toContain('id="panel-status"');
	});

	test("Ajustes mostra Wi-Fi e Diagnóstico, sem Bluetooth nem telemetria", () => {
		const markup = renderTab("ajustes", makeEnvelope());
		expect(markup).toContain('id="wifi"');
		expect(markup).toContain('id="diagnostics"');
		expect(markup).not.toContain('id="bluetooth"');
		expect(markup).not.toContain('id="value-speedKph"');
	});
});
