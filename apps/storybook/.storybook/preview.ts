import type { Preview } from "@storybook/react-vite";
import "../src/storybook.css";

const preview: Preview = {
	parameters: {
		layout: "centered",
		backgrounds: {
			options: { painel: { name: "painel", value: "#020617" } },
		},
	},
	initialGlobals: { backgrounds: { value: "painel" } },
};

export default preview;
