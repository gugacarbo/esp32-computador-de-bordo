import { createEnv } from "@t3-oss/env-core";
import { z } from "zod";

export const env = createEnv({
	server: {
		NODE_ENV: z.preprocess(
			(value) => (value === "" ? undefined : value),
			z.enum(["development", "test", "production"]).optional(),
		),
	},
	runtimeEnv: {
		NODE_ENV: process.env.NODE_ENV,
	},
});
