/**
 * lint-staged: cada tarefa recebe somente os paths staged e encaminha paths
 * explícitos. Os scripts globais de formatação não devem ser usados aqui.
 */
import { statSync } from "node:fs";

/** @param {string[]} files */
export function assertExplicitFiles(files) {
	for (const file of files) {
		const segments = file.split(/[\\/]+/);
		const isDirectory = (() => {
			try {
				return statSync(file).isDirectory();
			} catch {
				return false;
			}
		})();
		if (
			file === "." ||
			file === ".." ||
			segments.some((segment) => segment === "." || segment === "..") ||
			/[?*{}\[\]]/.test(file) ||
			file.endsWith("/") ||
			isDirectory
		) {
			throw new Error(`lint-staged recusou path não-explícito: ${JSON.stringify(file)}`);
		}
	}
	return files;
}

/** @param {string[]} files */
export function quoteFiles(files) {
	return files
		.map((file) => `'${file.replaceAll("'", "'\\''")}'`)
		.join(" ");
}

/** @param {string[]} files */
export function biomeFormatCommand(files) {
	if (files.length === 0) return [];
	assertExplicitFiles(files);
	return [`biome format --write --no-errors-on-unmatched -- ${quoteFiles(files)}`];
}

/** @param {string[]} files */
export function prettierMarkdownCommand(files) {
	if (files.length === 0) return [];
	assertExplicitFiles(files);
	return [`prettier --write --no-error-on-unmatched-pattern -- ${quoteFiles(files)}`];
}

/** @param {string[]} files */
export function cspellCommand(files) {
	if (files.length === 0) return [];
	assertExplicitFiles(files);
	return [
		`cspell lint --no-must-find-files --no-progress --no-show-suggestions -- ${quoteFiles(files)}`,
	];
}

/** @param {string[]} files */
export function jsTsCommands(files) {
	if (files.length === 0) return [];
	assertExplicitFiles(files);
	return [
		...biomeFormatCommand(files),
		...cspellCommand(files),
		"bun run typecheck",
		`bun run test-staged -- ${quoteFiles(files)}`,
		`bun scripts/test-visual-staged.ts ${quoteFiles(files)}`,
	];
}

/** @type {import('lint-staged').Configuration} */
export default {
	"*.{json,jsonc,css}": (files) => biomeFormatCommand(files),
	"*.{md,mdx}": (files) => [
		...prettierMarkdownCommand(files),
		...cspellCommand(files),
	],
	"*.{js,ts,jsx,tsx}": (files) => jsTsCommands(files),
};
