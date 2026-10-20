import type { ButtonHTMLAttributes, ReactElement } from "react";
import { cn } from "../../lib/utils";

/** Variantes visuais do botão. */
export type ButtonVariant = "primary" | "secondary" | "ghost";

const VARIANT_CLASSES: Record<ButtonVariant, string> = {
	primary: "bg-accent text-surface hover:bg-accent/90",
	secondary:
		"border border-line bg-surface-raised text-fg hover:bg-surface-raised/80",
	ghost: "text-fg-muted hover:bg-surface-raised hover:text-fg",
};

export interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
	/** Estilo visual: `primary` para a ação principal, `secondary` para ações comuns, `ghost` para ações discretas. */
	variant?: ButtonVariant;
}

/** Botão base do painel, com altura mínima de toque e foco visível. */
export function Button({
	className,
	variant = "primary",
	type = "button",
	...props
}: ButtonProps): ReactElement {
	return (
		<button
			type={type}
			className={cn(
				"inline-flex min-h-touch items-center justify-center gap-2 rounded-card px-4 text-sm font-semibold transition-colors",
				"focus-visible:outline focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-accent",
				"disabled:cursor-not-allowed disabled:opacity-50",
				VARIANT_CLASSES[variant],
				className,
			)}
			{...props}
		/>
	);
}
