import { expect, test } from "bun:test";
import { startupMessage } from "./index";

test("reports that the application is ready", () => {
	expect(startupMessage()).toMatch(
		/^ESP32 computador de bordo pronto(?: \(.+\))?$/,
	);
});
