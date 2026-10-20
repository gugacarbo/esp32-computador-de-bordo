import type { StorybookConfig } from "@storybook/react-vite";

const config: StorybookConfig = {
	// As stories ficam ao lado dos componentes do painel web.
	stories: ["../../web/src/**/*.stories.@(ts|tsx)"],
	framework: "@storybook/react-vite",
	addons: ["@storybook/addon-mcp"],
	features: { componentsManifest: true },
	async viteFinal(viteConfig) {
		const { default: tailwindcss } = await import("@tailwindcss/vite");
		viteConfig.plugins = [...(viteConfig.plugins ?? []), tailwindcss()];
		return viteConfig;
	},
};

export default config;
