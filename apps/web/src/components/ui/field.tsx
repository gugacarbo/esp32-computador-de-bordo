import { type InputHTMLAttributes, type ReactElement, useId } from "react";
import { cn } from "../../lib/utils";

export interface FieldProps extends InputHTMLAttributes<HTMLInputElement> {
	/** Rótulo visível associado ao campo via `htmlFor`. */
	label: string;
	/** Texto de ajuda exibido abaixo do campo e ligado ao campo por `aria-describedby`. */
	hint?: string;
	/** Id do parágrafo de ajuda; por padrão `<id>-ajuda`. Útil para manter ids estáveis referenciados por testes ou outros elementos. */
	hintId?: string;
	/** Mensagem de erro; marca o campo com `aria-invalid` e é exibida com o prefixo "Erro:". */
	error?: string;
}

/** Campo de texto com rótulo, ajuda e mensagem de erro acessíveis. */
export function Field({
	label,
	hint,
	hintId: hintIdProp,
	error,
	id,
	className,
	...props
}: FieldProps): ReactElement {
	const generatedId = useId();
	const inputId = id ?? generatedId;
	const hintId = hintIdProp ?? `${inputId}-ajuda`;
	const errorId = `${inputId}-erro`;
	const describedBy =
		[hint ? hintId : null, error ? errorId : null].filter(Boolean).join(" ") ||
		undefined;

	return (
		<div className="flex flex-col gap-1.5">
			<label htmlFor={inputId} className="text-sm font-medium text-fg">
				{label}
			</label>
			<input
				id={inputId}
				aria-invalid={error ? true : undefined}
				aria-describedby={describedBy}
				className={cn(
					"min-h-touch rounded-card border bg-surface px-3 text-sm text-fg placeholder:text-fg-muted",
					"focus-visible:outline focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-accent",
					"disabled:cursor-not-allowed disabled:opacity-50",
					error ? "border-status-error" : "border-line",
					className,
				)}
				{...props}
			/>
			{hint ? (
				<p id={hintId} className="text-xs text-fg-muted">
					{hint}
				</p>
			) : null}
			{error ? (
				<p id={errorId} className="text-xs text-status-error">
					<span className="font-semibold">Erro:</span> {error}
				</p>
			) : null}
		</div>
	);
}
