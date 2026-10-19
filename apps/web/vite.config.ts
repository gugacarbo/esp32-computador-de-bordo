import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";
import compression from "vite-plugin-compression";

export default defineConfig({
	plugins: [react(), tailwindcss(), compression({ algorithm: "gzip" })],
	build: {
		minify: "esbuild",
		sourcemap: false,
		target: "es2020",
		reportCompressedSize: false,
		chunkSizeWarningLimit: 600,
		assetsDir: "assets",
		rollupOptions: { output: { codeSplitting: false } },
	},
	server: {
		port: 5173,
	},
	preview: {
		port: 4173,
	},
});
