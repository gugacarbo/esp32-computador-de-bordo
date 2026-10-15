import type { HTMLAttributes, ReactElement } from "react";
import { cn } from "../../lib/utils";

export function Card({
	className,
	...props
}: HTMLAttributes<HTMLDivElement>): ReactElement {
	return (
		<div
			className={cn(
				"rounded-xl border border-slate-700/60 bg-slate-900 text-slate-100 shadow",
				className,
			)}
			{...props}
		/>
	);
}

export function CardHeader({
	className,
	...props
}: HTMLAttributes<HTMLDivElement>): ReactElement {
	return (
		<div
			className={cn("flex flex-col gap-1.5 p-4 pb-0", className)}
			{...props}
		/>
	);
}

export function CardTitle({
	className,
	...props
}: HTMLAttributes<HTMLHeadingElement>): ReactElement {
	return (
		<h2
			className={cn(
				"text-xs font-semibold uppercase tracking-wide text-slate-400",
				className,
			)}
			{...props}
		/>
	);
}

export function CardContent({
	className,
	...props
}: HTMLAttributes<HTMLDivElement>): ReactElement {
	return <div className={cn("p-4 pt-2", className)} {...props} />;
}
