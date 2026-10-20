import type { Preview } from "@storybook/react-vite";
import "../src/storybook.css";

const preview: Preview = {
	parameters: {
		layout: "centered",
		backgrounds: {
			options: { painel: { name: "painel", value: "#020617" } },
		},
		// Violações de acessibilidade (axe) falham os testes das stories.
		a11y: { test: "error" },
	},
	initialGlobals: { backgrounds: { value: "painel" } },
};

export default preview;
