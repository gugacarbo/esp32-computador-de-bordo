import { expect, test } from "bun:test";
import { createElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { TAB_LABELS, TABS } from "../../lib/tabs";
import { TabBar } from "./tab-bar";

test("TabBar (SSR) — três abas com rótulos", () => {
	const markup = renderToStaticMarkup(
		createElement(TabBar, { value: "telemetria" }),
	);
	for (const id of TABS) {
		expect(markup).toContain(TAB_LABELS[id]);
		expect(markup).toContain(`id="tab-${id}"`);
	}
});

test("TabBar (SSR) — somente a aba ativa fica selecionada", () => {
	const markup = renderToStaticMarkup(
		createElement(TabBar, { value: "conexao" }),
	);
	expect(markup).toContain(
		'<button type="button" id="tab-conexao" role="tab" aria-selected="true"',
	);
	expect(markup).toContain(
		'<button type="button" id="tab-telemetria" role="tab" aria-selected="false"',
	);
	expect(markup).toContain('id="tab-ajustes" role="tab" aria-selected="false"');
});

test("TabBar (SSR) — tablist aponta para o painel controlado", () => {
	const markup = renderToStaticMarkup(
		createElement(TabBar, { value: "telemetria" }),
	);
	expect(markup).toContain('role="tablist"');
	expect(markup).toContain('aria-controls="panel-telemetria"');
});
