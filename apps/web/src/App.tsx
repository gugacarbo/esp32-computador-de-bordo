import type { ReactElement } from "react";
import { DiagnosticsPanel } from "./components/DiagnosticsPanel";
import { SensorCard } from "./components/SensorCard";
import { Badge } from "./components/ui/badge";
import { useTelemetry } from "./hooks/useTelemetry";
import {
	PANEL_TEXT,
	type PanelState,
	type SensorKey,
	voltageTitle,
} from "./lib/envelope";
import { cn } from "./lib/utils";

const SENSOR_DEFS: { key: SensorKey; title: string }[] = [
	{ key: "rpm", title: "RPM" },
	{ key: "speedKph", title: "Velocidade" },
	{ key: "coolantC", title: "Temperatura do líquido de arrefecimento" },
	{ key: "throttlePercent", title: "Borboleta" },
	{ key: "mapKpa", title: "MAP" },
	{ key: "voltage", title: "Tensão" },
];

const PANEL_BORDER: Record<PanelState, string> = {
	loading: "border-l-slate-400",
	disconnected: "border-l-slate-400",
	adapter: "border-l-amber-300",
	ecu: "border-l-amber-300",
	connected: "border-l-emerald-400",
	"comm-error": "border-l-rose-400",
};

export default function App(): ReactElement {
	const { envelope, panel, transport, wsOnline } = useTelemetry();
	const offline = panel === "comm-error";
	const voltageSource = envelope?.voltage.source ?? "adapter";

	return (
		<div className="min-h-screen bg-[#0e1116] text-slate-100">
			<div className="mx-auto flex w-full max-w-5xl flex-col gap-3 px-3 py-4">
				<header className="flex flex-col gap-0.5 px-0.5">
					<h1 className="m-0 text-2xl font-bold tracking-widest">ONIX</h1>
					<p className="m-0 text-sm text-slate-400">Computador de bordo</p>
				</header>

				<section
					id="panel-status"
					data-state={panel}
					aria-live="polite"
					className={cn(
						"rounded-lg border border-slate-700/60 border-l-4 bg-slate-900 px-3 py-2 text-base font-semibold",
						PANEL_BORDER[panel],
					)}
				>
					{PANEL_TEXT[panel]}
				</section>

				<main id="grid" className="grid grid-cols-2 gap-2.5 md:grid-cols-3">
					{SENSOR_DEFS.map((def) => (
						<SensorCard
							key={def.key}
							sensor={def.key}
							title={
								def.key === "voltage" ? voltageTitle(voltageSource) : def.title
							}
							reading={envelope === null ? null : envelope[def.key]}
							offline={offline}
						/>
					))}
				</main>

				<footer className="flex flex-wrap items-center gap-2 px-0.5 text-xs text-slate-400">
					<Badge tone={wsOnline ? "ok" : "muted"}>
						{wsOnline ? "WebSocket" : "Polling /api/data"}
					</Badge>
					<span>
						{transport === "ws"
							? "Tempo real via ws://<host>:81/ws"
							: "Atualizando via GET /api/data"}
					</span>
					<span aria-hidden="true">·</span>
					<span>
						Uptime:{" "}
						{envelope === null
							? "—"
							: `${Math.floor(envelope.uptimeMs / 1000)}s`}
					</span>
				</footer>

				<DiagnosticsPanel />
			</div>
		</div>
	);
}
