import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";
import compression from "vite-plugin-compression";
import { viteSingleFile } from "vite-plugin-singlefile";

export default defineConfig({
	plugins: [
		react(),
		tailwindcss(),
		viteSingleFile(),
		compression({ algorithm: "gzip" }),
	],
	build: {
		minify: "esbuild",
		sourcemap: false,
		target: "es2020",
		reportCompressedSize: false,
		chunkSizeWarningLimit: 600,
	},
	server: {
		port: 5173,
	},
	preview: {
		port: 4173,
	},
});
