import type { HTMLAttributes, ReactElement } from "react";
import type { StatusTone } from "../../lib/envelope";
import { cn } from "../../lib/utils";

const TONE_CLASSES: Record<StatusTone, string> = {
	ok: "border-status-ok/30 bg-status-ok/10 text-status-ok",
	warn: "border-status-warn/30 bg-status-warn/10 text-status-warn",
	error: "border-status-error/30 bg-status-error/10 text-status-error",
	muted: "border-status-muted/30 bg-status-muted/10 text-status-muted",
};

/** Propriedades do {@link Badge}; aceita todos os atributos de `<span>`. */
interface BadgeProps extends HTMLAttributes<HTMLSpanElement> {
	/** Tom semântico do status: `ok`, `warn`, `error` ou `muted` (padrão). */
	tone?: StatusTone;
}

/**
 * Etiqueta compacta para exibir o estado de algo (ex.: conexão, sensor).
 * Renderiza um `<span>` em formato de pílula colorido pelo `tone`.
 */
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
