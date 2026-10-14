// dashboard.layout.spec.ts — SPEC-0003 §Conteúdo e apresentação: organização que caiba em telas
// de celular sem rolagem horizontal (caso 8: 320 e 360 pixels CSS) e aproveitamento de tela
// maior com três colunas. Usa o snapshot offline para exercitar os textos mais longos.
import { expect, test } from "@playwright/test";
import { offlineEnvelope, serialize } from "./api-payloads";
import { DashboardServer } from "./dashboard-server";

let server: DashboardServer;

test.beforeEach(async () => {
	server = await DashboardServer.start(serialize(offlineEnvelope()));
});

test.afterEach(() => {
	server.stop();
});

async function horizontalOverflow(page: import("@playwright/test").Page): Promise<number> {
	return page.evaluate(
		() => document.documentElement.scrollWidth - document.documentElement.clientWidth,
	);
}

async function gridColumns(page: import("@playwright/test").Page): Promise<number> {
	return page.evaluate(() => {
		const grid = document.getElementById("grid");
		if (!grid) {
			return 0;
		}
		return getComputedStyle(grid).gridTemplateColumns.split(" ").length;
	});
}

for (const width of [320, 360]) {
	test(`sem rolagem horizontal em ${width}px com duas colunas (caso 8)`, async ({ page }) => {
		await page.setViewportSize({ width, height: 640 });
		await page.goto(server.origin + "/");
		await expect(page.locator("#panel-status")).toHaveText("OBD desconectado");
		expect(await horizontalOverflow(page)).toBeLessThanOrEqual(0);
		expect(await gridColumns(page)).toBe(2);
		await expect(page.locator("#card-coolantC h2")).toBeVisible();
		await expect(page.locator("#state-rpm")).toBeVisible();
		await expect(page.locator("#note-voltage")).toBeVisible();
	});
}

test("tela maior usa três colunas sem rolagem horizontal (caso 8)", async ({ page }) => {
	await page.setViewportSize({ width: 1280, height: 800 });
	await page.goto(server.origin + "/");
	await expect(page.locator("#panel-status")).toHaveText("OBD desconectado");
	expect(await horizontalOverflow(page)).toBeLessThanOrEqual(0);
	expect(await gridColumns(page)).toBe(3);
	await expect(page.locator(".card")).toHaveCount(6);
});
