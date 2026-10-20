// App.tsx — painel mobile-first com navegação por abas inferiores (redesign).
// A troca de aba é estado local (sem router, bundle único, ADR-0010).
// Telemetria fica na aba inicial porque o uso dominante é dirigindo:
// números grandes legíveis de relance; config empurrada para Conexão/Ajustes.
import { type ReactElement, useState } from "react";
import { BluetoothPanel } from "./components/BluetoothPanel";
import { DiagnosticsPanel } from "./components/DiagnosticsPanel";
import { ScannerReconnect } from "./components/ScannerReconnect";
import { SensorCard } from "./components/SensorCard";
import { Badge } from "./components/ui/badge";
import { Card } from "./components/ui/card";
import { Stat } from "./components/ui/stat";
import { TabBar } from "./components/ui/tab-bar";
import { WifiPanel } from "./components/WifiPanel";
import { type Transport, useTelemetry } from "./hooks/useTelemetry";
import type { TelemetryEnvelope } from "./lib/envelope";
import {
	formatSensorValue,
	PANEL_TEXT,
	type PanelState,
	type SensorKey,
	type SensorReading,
	voltageTitle,
} from "./lib/envelope";
import { DEFAULT_TAB_ID, type TabId } from "./lib/tabs";
import { cn } from "./lib/utils";

/** Sensores secundários do herói; velocidade e RPM têm cartão destaque. */
const SECONDARY_SENSORS: { key: SensorKey; title: string }[] = [
	{ key: "coolantC", title: "Temperatura do líquido de arrefecimento" },
	{ key: "throttlePercent", title: "Borboleta" },
	{ key: "mapKpa", title: "MAP" },
	{ key: "voltage", title: "Tensão" },
];

/** Cor do ponto no chip de estado; o nome do estado vem de PANEL_TEXT. */
const PANEL_DOT: Record<PanelState, string> = {
	loading: "bg-fg-muted",
	disconnected: "bg-fg-muted",
	adapter: "bg-status-warn",
	ecu: "bg-status-warn",
	connected: "bg-status-ok",
	"comm-error": "bg-status-error",
};

/** Número do herói: leitura formatada só quando válida; senão traço. */
function heroDisplay(
	key: SensorKey,
	reading: SensorReading | null,
	offline: boolean,
): { value: string; tone: "muted" | undefined } {
	if (
		offline ||
		reading === null ||
		!reading.valid ||
		reading.status !== "ok" ||
		typeof reading.value !== "number"
	) {
		return { value: "—", tone: "muted" };
	}
	return {
		value: formatSensorValue(key, reading.value),
		tone: undefined,
	};
}

/** Cartão de relance: número grande centralizado com rótulo acima. */
function HeroCard({
	id,
	sensor,
	label,
	value,
	unit,
	tone,
	valueId,
	large = false,
}: {
	id: string;
	sensor: SensorKey;
	label: string;
	value: string;
	unit: string;
	tone: "muted" | undefined;
	valueId: string;
	large?: boolean;
}): ReactElement {
	return (
		<Card
			id={id}
			data-sensor={sensor}
			className="grid place-items-center px-4 py-5"
		>
			<span className="text-xs font-semibold uppercase tracking-wide text-fg-muted">
				{label}
			</span>
			<Stat
				bare
				size={large ? "xl" : "lg"}
				value={value}
				unit={unit}
				tone={tone}
				valueId={valueId}
				className="items-center"
			/>
		</Card>
	);
}

/** Aba Telemetria: velocidade e RPM de relance + sensores secundários. */
function TelemetryTab({
	envelope,
	offline,
	transport,
	wsOnline,
}: {
	envelope: TelemetryEnvelope | null;
	offline: boolean;
	transport: Transport;
	wsOnline: boolean;
}): ReactElement {
	const speed = heroDisplay("speedKph", envelope?.speedKph ?? null, offline);
	const rpm = heroDisplay("rpm", envelope?.rpm ?? null, offline);
	const voltageTitleText = voltageTitle(envelope?.voltage.source ?? "adapter");

	return (
		<section aria-label="Telemetria" className="flex flex-col gap-2.5">
			<HeroCard
				id="card-speedKph"
				sensor="speedKph"
				label="Velocidade"
				value={speed.value}
				unit="km/h"
				tone={speed.tone}
				valueId="value-speedKph"
				large
			/>
			<HeroCard
				id="card-rpm"
				sensor="rpm"
				label="RPM"
				value={rpm.value}
				unit="rpm"
				tone={rpm.tone}
				valueId="value-rpm"
			/>
			<div id="sensor-grid" className="grid grid-cols-2 gap-2.5 md:grid-cols-4">
				{SECONDARY_SENSORS.map((def) => (
					<SensorCard
						key={def.key}
						sensor={def.key}
						title={def.key === "voltage" ? voltageTitleText : def.title}
						reading={envelope === null ? null : envelope[def.key]}
						offline={offline}
					/>
				))}
			</div>
			<footer className="flex flex-wrap items-center gap-2 px-0.5 text-xs text-fg-muted">
				<Badge tone={wsOnline ? "ok" : "muted"}>
					{wsOnline ? "WebSocket" : "Polling /api/data"}
				</Badge>
				<span>
					{transport === "ws"
						? "Tempo real via ws://<host>:81/ws"
						: "Atualizando via GET /api/data"}
				</span>
				<span>
					Uptime:{" "}
					{envelope === null ? "—" : `${Math.floor(envelope.uptimeMs / 1000)}s`}
				</span>
			</footer>
		</section>
	);
}

/** Corpo do painel por aba: header com chip de estado sempre visível,
 * conteúdo da aba ativa e barra de abas fixa na base.
 */
export interface AppViewProps {
	/** Aba ativa controlada pelo estado local do App. */
	tab: TabId;
	/** Envelope de telemetria da placa; null enquanto não chega nada. */
	envelope: TelemetryEnvelope | null;
	/** Estado do painel OBD (mesmo do chip persistente). */
	panel: PanelState;
	/** Transporte ativo: WebSocket ou polling. */
	transport: Transport;
	/** True quando o WebSocket está aberto. */
	wsOnline: boolean;
	/** Callback da barra de abas; opcional em render estático (SSR/stories). */
	onTabChange?: (tab: TabId) => void;
}

/** Corpo do painel por aba: header com chip de estado sempre visível,
 * conteúdo da aba ativa e barra de abas fixa na base.
 */
export function AppView({
	tab,
	envelope,
	panel,
	transport,
	wsOnline,
	onTabChange,
}: AppViewProps): ReactElement {
	const offline = panel === "comm-error";

	return (
		<div className="flex min-h-screen flex-col bg-surface text-fg">
			<header className="flex flex-col gap-2 px-4 pb-2 pt-4">
				<div className="flex min-w-0 items-baseline justify-between gap-3">
					<h1 className="m-0 text-lg font-bold tracking-[0.2em]">ONIX</h1>
					<p className="m-0 truncate text-xs text-fg-muted">
						Computador de bordo
					</p>
				</div>
				<div
					id="panel-status"
					data-state={panel}
					aria-live="polite"
					className="flex max-w-fit items-center gap-2 rounded-full border border-line bg-surface-raised px-3 py-1.5 text-xs font-semibold"
				>
					<span
						aria-hidden="true"
						className={cn("size-2 shrink-0 rounded-full", PANEL_DOT[panel])}
					/>
					{PANEL_TEXT[panel]}
				</div>
			</header>

			<div
				role="tabpanel"
				id={`panel-${tab}`}
				aria-labelledby={`tab-${tab}`}
				className="mx-auto w-full max-w-3xl flex-1 px-3 pb-3"
			>
				{tab === "telemetria" && (
					<TelemetryTab
						envelope={envelope}
						offline={offline}
						transport={transport}
						wsOnline={wsOnline}
					/>
				)}
				{tab === "conexao" && (
					<div className="flex flex-col gap-2.5">
						<ScannerReconnect
							searchStopped={envelope?.btSearchStopped ?? false}
						/>
						<BluetoothPanel />
					</div>
				)}
				{tab === "ajustes" && (
					<div className="flex flex-col gap-2.5">
						<WifiPanel />
						<DiagnosticsPanel />
					</div>
				)}
			</div>

			<div className="sticky bottom-0 z-10 mt-2">
				<TabBar value={tab} onChange={onTabChange} />
			</div>
		</div>
	);
}

/** Raiz do painel: telemetria + estado persistente + abas. */
export default function App(): ReactElement {
	const { envelope, panel, transport, wsOnline } = useTelemetry();
	const [tab, setTab] = useState<TabId>(DEFAULT_TAB_ID);

	return (
		<AppView
			tab={tab}
			envelope={envelope}
			panel={panel}
			transport={transport}
			wsOnline={wsOnline}
			onTabChange={setTab}
		/>
	);
}
