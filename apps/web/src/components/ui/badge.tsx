import type { HTMLAttributes, ReactElement } from "react";
import type { StatusTone } from "../../lib/envelope";
import { cn } from "../../lib/utils";

const TONE_CLASSES: Record<StatusTone, string> = {
	ok: "border-emerald-400/30 bg-emerald-400/10 text-emerald-300",
	warn: "border-amber-300/30 bg-amber-300/10 text-amber-200",
	error: "border-rose-400/30 bg-rose-400/10 text-rose-300",
	muted: "border-slate-500/30 bg-slate-500/10 text-slate-400",
};

interface BadgeProps extends HTMLAttributes<HTMLSpanElement> {
	tone?: StatusTone;
}

export function Badge({
	className,
	tone = "muted",
	...props
}: BadgeProps): ReactElement {
	return (
		<span
			className={cn(
				"inline-flex items-center rounded-full border px-2 py-0.5 text-xs font-semibold",
				TONE_CLASSES[tone],
				className,
			)}
			{...props}
		/>
	);
}
