import type { HTMLAttributes, ReactElement } from "react";
import { cn } from "../../lib/utils";

/**
 * Contêiner de superfície para agrupar informações relacionadas.
 * Renderiza um `<div>` com borda, cantos arredondados e sombra.
 */
export function Card({
	className,
	...props
}: HTMLAttributes<HTMLDivElement>): ReactElement {
	return (
		<div
			className={cn(
				"rounded-card border border-line bg-surface-raised text-fg shadow",
				className,
			)}
			{...props}
		/>
	);
}

/**
 * Cabeçalho do {@link Card}, em coluna, com o título posicionado no topo.
 * Renderiza um `<div>`.
 */
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

/**
 * Título do {@link Card}, em caixa-alta e tamanho pequeno.
 * Renderiza um `<h2>`.
 */
export function CardTitle({
	className,
	...props
}: HTMLAttributes<HTMLHeadingElement>): ReactElement {
	return (
		<h2
			className={cn(
				"text-xs font-semibold uppercase tracking-wide text-fg-muted",
				className,
			)}
			{...props}
		/>
	);
}

/**
 * Corpo do {@link Card}, com preenchimento padrão.
 * Renderiza um `<div>`.
 */
export function CardContent({
	className,
	...props
}: HTMLAttributes<HTMLDivElement>): ReactElement {
	return <div className={cn("p-4 pt-2", className)} {...props} />;
}
