import type { HTMLAttributes, ReactElement } from "react";
import type { StatusTone } from "../../lib/envelope";
import { cn } from "../../lib/utils";

const TONE_CLASSES: Record<StatusTone, string> = {
	ok: "border-status-ok/30 bg-status-ok/10 text-status-ok",
	warn: "border-status-warn/30 bg-status-warn/10 text-status-warn",
	error: "border-status-error/30 bg-status-error/10 text-status-error",
	muted: "border-line bg-surface-raised text-status-muted",
};

/** Prefixo textual por tom, para não comunicar o estado só por cor. */
const TONE_PREFIX: Record<StatusTone, string> = {
	ok: "OK:",
	warn: "Atenção:",
	error: "Erro:",
	muted: "Info:",
};

export interface AlertProps extends HTMLAttributes<HTMLDivElement> {
	/** Gravidade da mensagem; `error` e `warn` são anunciados imediatamente (`role="alert"`). */
	tone?: StatusTone;
	/** Título curto opcional em destaque acima do conteúdo. */
	title?: string;
	/** Exibe o prefixo textual do tom (ex.: "Erro:"). Desligue só quando o texto da mensagem já deixar o estado explícito. */
	showPrefix?: boolean;
}

/** Mensagem de estado em destaque, com prefixo textual e tom semântico. */
export function Alert({
	className,
	tone = "muted",
	title,
	showPrefix = true,
	children,
	...props
}: AlertProps): ReactElement {
	const urgent = tone === "error" || tone === "warn";
	return (
		<div
			role={urgent ? "alert" : "status"}
			className={cn(
				"flex gap-3 rounded-card border p-3 text-sm",
				TONE_CLASSES[tone],
				className,
			)}
			{...props}
		>
			{showPrefix ? (
				<span className="font-semibold">{TONE_PREFIX[tone]}</span>
			) : null}
			<div className="flex min-w-0 flex-col gap-0.5 text-fg">
				{title ? <p className="font-semibold">{title}</p> : null}
				{children ? <div>{children}</div> : null}
			</div>
		</div>
	);
}
