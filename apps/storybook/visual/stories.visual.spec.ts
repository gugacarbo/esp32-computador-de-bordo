// cspell:ignore networkidle
import { readFileSync } from "node:fs";
import { expect, test } from "@playwright/test";

// Um teste por story (ignora docs). A lista vem do index.json do build estático, lido
// aqui na carga do spec. O build é garantido antes disso por playwright.storybook.config.ts,
// que roda o build ao carregar o config (a coleta de testes acontece depois dele).
interface IndexEntry {
	type: string;
	id: string;
	title: string;
	name: string;
}

const indexUrl = new URL("../storybook-static/index.json", import.meta.url);
const index = JSON.parse(readFileSync(indexUrl, "utf8")) as { entries: Record<string, IndexEntry> };
const stories = Object.values(index.entries).filter((entry) => entry.type === "story");

for (const story of stories) {
	test(`${story.title} / ${story.name} (${story.id})`, async ({ page }) => {
		await page.goto(`/iframe.html?id=${story.id}&viewMode=story`);

		// Aguarda o render da story, fontes e rede ociosa antes do snapshot.
		await expect(page.locator("#storybook-root")).not.toBeEmpty();
		await page.waitForLoadState("networkidle");
		await page.evaluate(() => document.fonts.ready);

		await expect(page).toHaveScreenshot(`${story.id}.png`, { fullPage: true });
	});
}
