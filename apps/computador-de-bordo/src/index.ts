import { env } from "./env";

export function startupMessage(): string {
	return `ESP32 computador de bordo pronto${env.NODE_ENV ? ` (${env.NODE_ENV})` : ""}`;
}

if (import.meta.main) {
	process.stdout.write(`${startupMessage()}\n`);
}
