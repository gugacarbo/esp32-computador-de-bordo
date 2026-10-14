// dashboard.render.spec.ts — SPEC-0003 §Conteúdo e apresentação / §Estado do painel e dos cartões.
// Cobre os casos de borda 1–5, 9 e 10 sobre o HTML real extraído de include/dashboard.h,
// servido pelo fixture local com envelopes da SPEC-0002 (api-payloads.ts).
import { expect, test } from "@playwright/test";
import {
	CONNECTED_UPTIME_MS,
	SENSOR_KEYS,
	absent,
	adapterStartingEnvelope,
	connectedEnvelope,
	ecuSilentEnvelope,
	failedValue,
	ok,
	offlineEnvelope,
	serialize,
	staleValue,
	withVoltageSource,
} from "./api-payloads";
import { DashboardServer } from "./dashboard-server";
import { EXTERNAL_RESOURCE_PATTERN } from "./dashboard-source";

let server: DashboardServer;

test.beforeEach(async () => {
	server = await DashboardServer.start(serialize(offlineEnvelope()));
});

test.afterEach(() => {
	server.stop();
});

test("mostra estrutura e carregamento antes do primeiro snapshot (caso 1)", async ({ page }) => {
	server.queueOnce({ delayMs: 2500 });
	await page.goto(server.origin + "/");
	await expect(page.locator("header h1")).toHaveText("ONIX");
	await expect(page.locator("header p")).toHaveText("Computador de bordo");
	await expect(page.locator("#panel-status")).toHaveText("Carregando dados");
	await expect(page.locator("#panel-status")).toHaveAttribute("data-state", "loading");
	await expect(page.locator(".card")).toHaveCount(6);
	for (const key of SENSOR_KEYS) {
		await expect(page.locator(`#value-${key}`)).toHaveText("—");
		await expect(page.locator(`#state-${key}`)).toHaveText("");
		await expect(page.locator(`#note-${key}`)).toHaveText("");
	}
	await expect(page.locator("button")).toHaveCount(0);
	expect(EXTERNAL_RESOURCE_PATTERN.test(server.html)).toBe(false);
});

test("aplica snapshot offline e diferencia falta de leitura (casos 1–2)", async ({ page }) => {
	await page.goto(server.origin + "/");
	await expect(page.locator("#panel-status")).toHaveText("OBD desconectado");
	await expect(page.locator("#panel-status")).toHaveAttribute("data-state", "disconnected");
	await expect(page.locator("#label-voltage")).toHaveText("Tensão do adaptador");
	for (const key of SENSOR_KEYS) {
		await expect(page.locator(`#value-${key}`)).toHaveText("—");
		await expect(page.locator(`#state-${key}`)).toHaveText("Sem leitura");
		await expect(page.locator(`#note-${key}`)).toHaveText("");
	}
});

test("renderiza leituras, zero legítimo, não suportado e origem da tensão (casos 3, 10)", async ({
	page,
}) => {
	const envelope = withVoltageSource(
		connectedEnvelope({ coolantC: ok(91.6, CONNECTED_UPTIME_MS) }),
		"ecu",
	);
	server.setData(serialize(envelope));
	await page.goto(server.origin + "/");
	await expect(page.locator("#panel-status")).toHaveText("OBD conectado");
	await expect(page.locator("#value-rpm")).toHaveText("842");
	await expect(page.locator("#value-speedKph")).toHaveText("0");
	await expect(page.locator("#value-coolantC")).toHaveText("92");
	await expect(page.locator("#state-throttlePercent")).toHaveText("Não suportado");
	await expect(page.locator("#value-throttlePercent")).toHaveText("—");
	await expect(page.locator("#value-mapKpa")).toHaveText("31");
	await expect(page.locator("#value-voltage")).toHaveText("14.1");
	await expect(page.locator("#label-voltage")).toHaveText("Tensão da ECU");
	await expect(page.locator("#state-rpm")).toHaveText("");
	await expect(page.locator("#note-rpm")).toHaveText("");
});

test("diferencia fases sem anunciar OBD conectado (caso 5)", async ({ page }) => {
	server.setData(serialize(adapterStartingEnvelope()));
	await page.goto(server.origin + "/");
	await expect(page.locator("#panel-status")).toHaveText("Inicializando adaptador");
	await expect(page.locator("#panel-status")).toHaveAttribute("data-state", "adapter");
	server.setData(serialize(ecuSilentEnvelope()));
	await expect(page.locator("#panel-status")).toHaveText("ECU sem resposta");
	await expect(page.locator("#panel-status")).toHaveAttribute("data-state", "ecu");
	server.setData(serialize(connectedEnvelope()));
	await expect(page.locator("#panel-status")).toHaveText("OBD conectado");
	await expect(page.locator("#state-throttlePercent")).toHaveText("Não suportado");
});

test("identifica vencido, falha e leitura anterior (casos 2, 4)", async ({ page }) => {
	const envelope = connectedEnvelope({
		rpm: staleValue(842, CONNECTED_UPTIME_MS),
		speedKph: failedValue(73, "timeout", CONNECTED_UPTIME_MS),
		coolantC: absent(),
	});
	server.setData(serialize(envelope));
	await page.goto(server.origin + "/");
	await expect(page.locator("#value-rpm")).toHaveText("—");
	await expect(page.locator("#state-rpm")).toHaveText("Desatualizado");
	await expect(page.locator("#note-rpm")).toHaveText("Leitura anterior: 842 rpm");
	await expect(page.locator("#state-speedKph")).toHaveText("Falha na leitura");
	await expect(page.locator("#note-speedKph")).toHaveText("Leitura anterior: 73 km/h");
	await expect(page.locator("#state-coolantC")).toHaveText("Sem leitura");
	await expect(page.locator("#note-coolantC")).toHaveText("");
	await expect(page.locator("#value-mapKpa")).toHaveText("31");
});

test("carrega tudo da origem local e não requisita nada externo (caso 9)", async ({ page }) => {
	const external: string[] = [];
	await page.route("**/*", async (route) => {
		const url = route.request().url();
		if (url.startsWith(server.origin)) {
			await route.continue();
			return;
		}
		external.push(url);
		await route.abort();
	});
	await page.goto(server.origin + "/");
	await expect(page.locator("#panel-status")).toHaveText("OBD desconectado");
	await page.waitForTimeout(1200);
	expect(external).toEqual([]);
	expect(server.paths.length).toBeGreaterThan(0);
});
