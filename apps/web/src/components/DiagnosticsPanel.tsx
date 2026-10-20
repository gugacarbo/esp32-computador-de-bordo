import { type ReactElement, useEffect, useState } from "react";
import {
	formatUptime,
	LOGS_PATH,
	type LogEntry,
	type LogLevel,
	type LogsSnapshot,
	parseLogsText,
} from "../lib/logs";
import { cn } from "../lib/utils";
import { Alert } from "./ui/alert";

const POLL_MS = 3000;
const FETCH_TIMEOUT_MS = 2000;

const LEVEL_LABEL: Record<LogLevel, string> = {
	error: "ERRO",
	warn: "AVISO",
	info: "INFO",
	debug: "DEBUG",
};

const LEVEL_CLASSES: Record<LogLevel, string> = {
	error: "border-status-error/40 bg-status-error/10 text-status-error",
	warn: "border-status-warn/40 bg-status-warn/10 text-status-warn",
	info: "border-line bg-surface-raised text-fg",
	debug: "border-line bg-transparent text-status-muted",
};

function LogRow({ entry }: { entry: LogEntry }): ReactElement {
	return (
		<li className="flex min-w-0 flex-col gap-0.5 border-t border-line py-1.5 first:border-t-0">
			<div className="flex min-w-0 flex-wrap items-center gap-x-2 gap-y-0.5">
				<span className="tabular-nums text-fg-muted">
					{formatUptime(entry.uptimeMs)}
				</span>
				<span
					className={cn(
						"rounded border px-1 text-[10px] font-semibold leading-4",
						LEVEL_CLASSES[entry.level],
					)}
				>
					{LEVEL_LABEL[entry.level]}
				</span>
				<span className="min-w-0 break-words text-accent">{entry.tag}</span>
			</div>
			<p className="m-0 min-w-0 break-words text-fg">{entry.message}</p>
		</li>
	);
}

/**
 * Seção recolhida "Diagnóstico": enquanto aberta, consulta GET /api/logs a
 * cada ~3s (sem requisições sobrepostas) e mostra as entradas mais recentes
 * primeiro. Para de consultar ao fechar ou desmontar.
 */
export function DiagnosticsPanel(): ReactElement {
	const [open, setOpen] = useState(false);
	const [snapshot, setSnapshot] = useState<LogsSnapshot | null>(null);
	const [error, setError] = useState<string | null>(null);

	useEffect(() => {
		if (!open) return;
		let disposed = false;
		let timer = 0;
		let inFlight: AbortController | null = null;

		const poll = async (): Promise<void> => {
			const started = Date.now();
			const ctrl = new AbortController();
			inFlight = ctrl;
			const timeout = window.setTimeout(() => ctrl.abort(), FETCH_TIMEOUT_MS);
			try {
				const response = await fetch(LOGS_PATH, {
					cache: "no-store",
					signal: ctrl.signal,
				});
				if (!response.ok) throw new Error(`http ${response.status}`);
				const parsed = parseLogsText(await response.text());
				if (parsed === null) throw new Error("formato");
				if (!disposed) {
					setSnapshot(parsed);
					setError(null);
				}
			} catch {
				if (!disposed) setError("Falha ao obter logs; tentando novamente");
			} finally {
				window.clearTimeout(timeout);
				inFlight = null;
				if (!disposed) {
					const elapsed = Date.now() - started;
					timer = window.setTimeout(
						() => void poll(),
						Math.max(0, POLL_MS - elapsed),
					);
				}
			}
		};

		void poll();

		return () => {
			disposed = true;
			window.clearTimeout(timer);
			inFlight?.abort();
		};
	}, [open]);

	const entries = snapshot === null ? [] : [...snapshot.entries].reverse();

	return (
		<details
			id="diagnostics"
			className="rounded-lg border border-line/60 bg-surface-raised px-3 py-2 text-sm"
			onToggle={(event) => setOpen(event.currentTarget.open)}
		>
			<summary className="cursor-pointer select-none font-semibold text-fg">
				Diagnóstico
			</summary>
			<div className="mt-2 flex flex-col gap-2 font-mono text-xs">
				{error !== null && (
					<Alert
						tone="error"
						showPrefix={false}
						role="alert"
						className="break-words"
					>
						{error}
					</Alert>
				)}
				{snapshot === null && error === null && (
					<p className="m-0 text-fg-muted">Carregando logs…</p>
				)}
				{snapshot !== null && snapshot.dropped > 0 && (
					<p className="m-0 text-status-warn">
						{snapshot.dropped} entradas descartadas
					</p>
				)}
				{snapshot !== null && entries.length === 0 && (
					<p className="m-0 text-fg-muted">Nenhuma entrada registrada.</p>
				)}
				{entries.length > 0 && (
					<ul className="m-0 flex max-h-96 min-w-0 list-none flex-col overflow-y-auto p-0">
						{entries.map((entry) => (
							<LogRow
								key={`${entry.uptimeMs}-${entry.level}-${entry.tag}-${entry.message}`}
								entry={entry}
							/>
						))}
					</ul>
				)}
			</div>
		</details>
	);
}
