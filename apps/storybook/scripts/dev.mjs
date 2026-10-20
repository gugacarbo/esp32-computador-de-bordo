// Inicia o Storybook na porta definida em STORYBOOK_PORT (.env da raiz).
import { spawn } from "node:child_process";

try {
	process.loadEnvFile(new URL("../../../.env", import.meta.url));
} catch {
	// .env é opcional: vale o padrão abaixo.
}

const port = process.env.STORYBOOK_PORT || "6606";
const child = spawn("storybook", ["dev", "-p", port], { stdio: "inherit" });
child.on("exit", (code) => process.exit(code ?? 1));
