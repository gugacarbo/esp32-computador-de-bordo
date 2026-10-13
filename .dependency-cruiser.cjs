const fs = require("node:fs");

const appsDirectory = "apps";
const appNames = fs.existsSync(appsDirectory)
	? fs
			.readdirSync(appsDirectory, { withFileTypes: true })
			.filter((entry) => entry.isDirectory())
			.map((entry) => entry.name)
		: [];

/** @type {import('dependency-cruiser').IConfiguration} */
module.exports = {
	forbidden: [
		{
			name: "no-circular",
			severity: "error",
			from: { pathNot: "^node_modules/" },
			to: { circular: true },
		},
		...appNames.map((appName) => {
			const escapedAppName = appName.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
			return {
				name: `no-sibling-app-imports-${appName}`,
				severity: "error",
				from: { path: `^apps/${escapedAppName}/` },
				to: { path: `^apps/(?!${escapedAppName}/)[^/]+/` },
			};
		}),
		{
			name: "domain-no-infrastructure-or-runtime",
			severity: "error",
			from: { path: "(^|/)domain/" },
			to: { path: "(^|/)(infrastructure|runtime|adapters)/" },
		},
		{
			name: "no-package-deep-imports",
			severity: "error",
			from: { path: "^(apps|packages)/" },
			to: { path: "^packages/[^/]+/src/" },
		},
	],
	options: {
		combinedDependencies: true,
		doNotFollow: { path: "^node_modules/" },
	},
};
