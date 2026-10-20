import type { HTMLAttributes, ReactElement } from "react";
import type { StatusTone } from "../../lib/envelope";
import { cn } from "../../lib/utils";

const TONE_CLASSES: Record<StatusTone, string> = {
	ok: "text-status-ok",
	warn: "text-status-warn",
	error: "text-status-error",
	muted: "text-status-muted",
};

export interface StatProps extends HTMLAttributes<HTMLDivElement> {
	/** Rótulo descritivo opcional exibido acima do valor (ex.: "Rotação"). */
	label?: string;
	/** Valor principal em destaque; use "—" quando não houver leitura. */
	value: string | number;
	/** Unidade exibida ao lado do valor (ex.: "rpm", "km/h"). */
	unit?: string;
	/** Cor semântica do valor; sem tom, usa a cor de texto padrão. */
	tone?: StatusTone;
	/** Id aplicado apenas ao elemento que contém o valor (sem a unidade), para leitura/atualização externa. */
	valueId?: string;
	/** Tamanho do valor: `xl` (text-6xl, herói de relance), `lg` (padrão, text-4xl) ou `md` (text-3xl). */
	size?: "md" | "lg" | "xl";
	/** Remove moldura, fundo e espaçamento, para uso dentro de outro cartão. */
	bare?: boolean;
}

/** Valor numérico grande com rótulo e unidade, para telemetria. */
export function Stat({
	className,
	label,
	value,
	unit,
	tone,
	valueId,
	size = "lg",
	bare = false,
	...props
}: StatProps): ReactElement {
	return (
		<div
			className={cn(
				"flex flex-col gap-1",
				!bare && "rounded-card border border-line bg-surface p-4",
				className,
			)}
			{...props}
		>
			{label ? (
				<span className="text-xs font-semibold uppercase tracking-wide text-fg-muted">
					{label}
				</span>
			) : null}
			<span
				className={cn(
					"flex items-baseline gap-1 font-mono font-bold tabular-nums",
					size === "lg" ? "text-4xl" : size === "xl" ? "text-6xl" : "text-3xl",
					tone ? TONE_CLASSES[tone] : "text-fg",
				)}
			>
				<span id={valueId}>{value}</span>
				{unit ? (
					<span className="text-base font-medium text-fg-muted">{unit}</span>
				) : null}
			</span>
		</div>
	);
}
