import { expect, test } from "bun:test";
import { createElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { Stat } from "./stat";

test("Stat (SSR) — tamanho padrão grande", () => {
	const markup = renderToStaticMarkup(
		createElement(Stat, { value: 12, label: "Rotação" }),
	);
	expect(markup).toContain("text-4xl");
});

test("Stat (SSR) — tamanho xl para o herói de velocidade", () => {
	const markup = renderToStaticMarkup(
		createElement(Stat, { value: 87, valueId: "value-speedKph", size: "xl" }),
	);
	expect(markup).toContain("text-6xl");
	expect(markup).toContain('id="value-speedKph"');
});
