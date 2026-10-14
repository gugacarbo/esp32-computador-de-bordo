// dashboard.polling.spec.ts — SPEC-0003 §Atualização: no máximo uma requisição em andamento,
// agendamento da próxima somente após a anterior terminar, timeout do cliente, JSON/estrutura
// inválidos, recuperação automática e retomada de aba suspensa sem rajada (casos 6, 7 e 11).
import { expect, test } from "@playwright/test";
import {
	connectedEnvelope,
	draftOf,
	serialize,
	serializeDraft,
} from "./api-payloads";
import { DashboardServer } from "./dashboard-server";

let server: DashboardServer;

test.beforeEach(async () => {
	server = await DashboardServer.start(serialize(connectedEnvelope()));
});

test.afterEach(() => {
	server.stop();
});

test("consulta em cadência de ~500ms com no máximo uma requisição (casos 6–7)", async ({
	page,
}) => {
	await page.goto(server.origin + "/");
	await expect(page.locator("#value-rpm")).toHaveText("842");
	await page.waitForTimeout(1600);
	expect(server.dataRequests.length).toBeGreaterThanOrEqual(3);
	expect(server.dataRequests.length).toBeLessThanOrEqual(6);
	expect(server.maxInFlight).toBe(1);
});

test("resposta lenta não sobrepõe consultas nem derruba o estado (caso 6)", async ({ page }) => {
	await page.goto(server.origin + "/");
	await expect(page.locator("#panel-status")).toHaveText("OBD conectado");
	const base = server.dataRequests.length;
	server.queueOnce({ delayMs: 2500 });
	await page.waitForTimeout(2000);
	expect(server.dataRequests.length).toBe(base);
	expect(server.maxInFlight).toBe(1);
	await page.waitForTimeout(1600);
	expect(server.dataRequests.length).toBeGreaterThan(base);
	await expect(page.locator("#panel-status")).toHaveText("OBD conectado");
	await expect(page.locator("#value-rpm")).toHaveText("842");
});

test("erro HTTP vira falha de comunicação e recupera sozinho (casos 6–7)", async ({ page }) => {
	await page.goto(server.origin + "/");
	await expect(page.locator("#value-rpm")).toHaveText("842");
	server.queueFor(4, { status: 503, body: "service unavailable" });
	await expect(page.locator("#panel-status")).toHaveText("Sem comunicação com o ESP32");
	await expect(page.locator("#panel-status")).toHaveAttribute("data-state", "comm-error");
	await expect(page.locator("#value-rpm")).toHaveText("—");
	await expect(page.locator("#note-rpm")).toHaveText("Leitura anterior: 842 rpm");
	await expect(page.locator("#panel-status")).toHaveText("OBD conectado", { timeout: 15000 });
	await expect(page.locator("#value-rpm")).toHaveText("842");
	await expect(page.locator("#note-rpm")).toHaveText("");
	expect(server.maxInFlight).toBe(1);
});

test("JSON inválido é falha de comunicação e não apaga a tela (caso 6)", async ({ page }) => {
	await page.goto(server.origin + "/");
	await expect(page.locator("#value-rpm")).toHaveText("842");
	server.queueOnce({ body: "<html>resposta inesperada</html>" });
	await expect(page.locator("#panel-status")).toHaveText("Sem comunicação com o ESP32");
	await expect(page.locator("#value-rpm")).toHaveText("—");
	await expect(page.locator("#note-rpm")).toHaveText("Leitura anterior: 842 rpm");
	await expect(page.locator("#panel-status")).toHaveText("OBD conectado", { timeout: 15000 });
	await expect(page.locator("#value-rpm")).toHaveText("842");
	expect(server.maxInFlight).toBe(1);
});

test("resposta sem estrutura obrigatória é tratada como falha (caso 6)", async ({ page }) => {
	await page.goto(server.origin + "/");
	await expect(page.locator("#value-rpm")).toHaveText("842");
	const missingField = draftOf(connectedEnvelope());
	delete (missingField as Record<string, unknown>).rpm;
	server.queueOnce({ body: serializeDraft(missingField) });
	await expect(page.locator("#panel-status")).toHaveText("Sem comunicação com o ESP32");
	await expect(page.locator("#panel-status")).toHaveText("OBD conectado", { timeout: 15000 });
	const inconsistent = draftOf(connectedEnvelope());
	inconsistent.rpm = { ...inconsistent.rpm, valid: true, status: "stale" };
	server.queueOnce({ body: serializeDraft(inconsistent) });
	await expect(page.locator("#panel-status")).toHaveText("Sem comunicação com o ESP32");
	await expect(page.locator("#panel-status")).toHaveText("OBD conectado", { timeout: 15000 });
	expect(server.maxInFlight).toBe(1);
});

test("consulta que passa do timeout é encerrada e depois recupera (caso 6)", async ({ page }) => {
	await page.goto(server.origin + "/");
	await expect(page.locator("#value-rpm")).toHaveText("842");
	server.queueOnce({ delayMs: 4200 });
	await expect(page.locator("#panel-status")).toHaveText("Sem comunicação com o ESP32", {
		timeout: 8000,
	});
	await expect(page.locator("#value-rpm")).toHaveText("—");
	await expect(page.locator("#note-rpm")).toHaveText("Leitura anterior: 842 rpm");
	await expect(page.locator("#panel-status")).toHaveText("OBD conectado", { timeout: 10000 });
	await expect(page.locator("#value-rpm")).toHaveText("842");
	expect(server.maxInFlight).toBe(1);
});

test("aba suspensa retoma sem rajada e sem timers duplicados (caso 11)", async ({ page }) => {
	await page.goto(server.origin + "/");
	await expect(page.locator("#value-rpm")).toHaveText("842");
	const goHidden = () =>
		page.evaluate(() => {
			Object.defineProperty(document, "visibilityState", {
				configurable: true,
				get: () => "hidden",
			});
			document.dispatchEvent(new Event("visibilitychange"));
		});
	const goVisible = () =>
		page.evaluate(() => {
			Object.defineProperty(document, "visibilityState", {
				configurable: true,
				get: () => "visible",
			});
			document.dispatchEvent(new Event("visibilitychange"));
		});
	const base = server.dataRequests.length;
	await goHidden();
	await page.waitForTimeout(1500);
	expect(server.dataRequests.length - base).toBeLessThanOrEqual(1);
	await goVisible();
	await page.waitForTimeout(2000);
	expect(server.dataRequests.length - base).toBeGreaterThanOrEqual(3);
	expect(server.dataRequests.length - base).toBeLessThanOrEqual(6);
	for (let i = 0; i < 3; i += 1) {
		await goHidden();
		await page.waitForTimeout(200);
		await goVisible();
	}
	await page.waitForTimeout(1500);
	expect(server.dataRequests.length - base).toBeLessThanOrEqual(12);
	expect(server.maxInFlight).toBe(1);
});
