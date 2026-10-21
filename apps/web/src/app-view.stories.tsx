import type { Meta, StoryObj } from "@storybook/react-vite";
import type { ReactElement } from "react";
import type { AppViewProps } from "./App";
import { AppView } from "./App";
import type {
	PanelState,
	SensorKey,
	TelemetryEnvelope,
	VoltageSource,
} from "./lib/envelope";
import type { TabId } from "./lib/tabs";

const DEFAULT_ENVELOPE: TelemetryEnvelope = {
	uptimeMs: 184_500,
	btConnected: true,
	adapterReady: true,
	ecuResponding: true,
	obdConnected: true,
	btSearchStopped: false,
	rpm: { value: 2450, valid: true, status: "ok", updatedAtMs: 20, ageMs: 55 },
	speedKph: {
		value: 87,
		valid: true,
		status: "ok",
		updatedAtMs: 20,
		ageMs: 40,
	},
	coolantC: {
		value: 91,
		valid: true,
		status: "ok",
		updatedAtMs: 15,
		ageMs: 60,
	},
	throttlePercent: {
		value: 18,
		valid: true,
		status: "ok",
		updatedAtMs: 20,
		ageMs: 50,
	},
	mapKpa: { value: 35, valid: true, status: "ok", updatedAtMs: 20, ageMs: 45 },
	voltage: {
		value: 13.9,
		valid: true,
		status: "ok",
		updatedAtMs: 15,
		ageMs: 55,
		source: "adapter",
	},
};

function lerNumero(valor: string | undefined | null): number | null {
	if (valor === undefined || valor === null || valor === "") return null;
	const n = Number(valor);
	return Number.isFinite(n) ? n : null;
}

type PainelDemoArgs = Partial<AppViewProps> & {
	velocidade?: string | null;
	rpm?: string | null;
	coolant?: string | null;
	borboleta?: string | null;
	map?: string | null;
	tensao?: string | null;
	uptime?: string | null;
	btSearchStopped?: boolean;
};

function PainelDemo(args: PainelDemoArgs): ReactElement {
	const panel: PanelState = args.panel ?? "connected";
	const env: TelemetryEnvelope = {
		...DEFAULT_ENVELOPE,
		uptimeMs: lerNumero(args.uptime ?? null) ?? 0,
		btSearchStopped: args.btSearchStopped ?? false,
		speedKph: leitura("speedKph", args.velocidade ?? null),
		rpm: leitura("rpm", args.rpm ?? null),
		coolantC: leitura("coolantC", args.coolant ?? null),
		throttlePercent: leitura("throttlePercent", args.borboleta ?? null),
		mapKpa: leitura("mapKpa", args.map ?? null),
		voltage: {
			...DEFAULT_ENVELOPE.voltage,
			value: lerNumero(args.tensao ?? null) ?? DEFAULT_ENVELOPE.voltage.value,
			source: "adapter" as VoltageSource,
		},
	};
	return (
		<AppView
			tab={args.tab ?? "telemetria"}
			envelope={panel === "comm-error" ? null : env}
			panel={panel}
		/>
	);
}

function leitura(key: SensorKey, texto: string | null) {
	const valor = lerNumero(texto);
	return {
		value: valor,
		valid: valor !== null,
		status: (valor !== null ? "ok" : "unavailable") as
			| "ok"
			| "unavailable"
			| "unsupported"
			| "stale"
			| "timeout"
			| "error",
		updatedAtMs: valor !== null ? Date.now() : null,
		ageMs: valor !== null ? 40 : null,
	};
}

const TABS_VALUES: TabId[] = ["telemetria", "conexao", "ajustes"];

const meta = {
	title: "App/Painel",
	component: PainelDemo,
	tags: ["autodocs"],
	args: {
		tab: "telemetria",
		panel: "connected",
		velocidade: "87",
		rpm: "2450",
		coolant: "91",
		borboleta: "18",
		map: "35",
		tensao: "13.9",
		uptime: "184500",
		btSearchStopped: false,
	},
	argTypes: {
		tab: {
			control: "select",
			options: TABS_VALUES,
			description: "Aba ativa (sem router, estado local).",
		},
		panel: {
			control: "select",
			options: [
				"loading",
				"disconnected",
				"adapter",
				"ecu",
				"connected",
				"comm-error",
			] as const,
			description: "Estado do OBD (chip persistente no cabeçalho).",
		},
		velocidade: { control: "text", description: "Velocidade simulada." },
		rpm: { control: "text", description: "RPM simulado." },
		coolant: { control: "text", description: "Temp. líquido simulada." },
		borboleta: { control: "text", description: "Borboleta simulada." },
		map: { control: "text", description: "MAP simulado." },
		tensao: { control: "text", description: "Tensão simulada." },
		uptime: { control: "text", description: "Uptime (s)." },
		btSearchStopped: {
			control: "boolean",
			description: "Busca BT parada (aviso na aba Conexão).",
		},
	},
} satisfies Meta<typeof PainelDemo>;

export default meta;
type Story = StoryObj<typeof meta>;

export const Padrao: Story = {};

export const SemComunicacao: Story = {
	args: {
		panel: "comm-error",
		velocidade: null,
		rpm: null,
		coolant: null,
		borboleta: null,
		map: null,
		tensao: null,
		uptime: null,
		btSearchStopped: false,
	},
};

export const Carregando: Story = {
	args: {
		panel: "loading",
		velocidade: null,
		rpm: null,
		coolant: null,
		borboleta: null,
		map: null,
		tensao: null,
		uptime: null,
		btSearchStopped: false,
	},
};

export const OBDDesconectado: Story = {
	args: {
		panel: "disconnected",
		velocidade: null,
		rpm: null,
		coolant: null,
		borboleta: null,
		map: null,
		tensao: null,
		uptime: null,
		btSearchStopped: true,
	},
};

export const AquecendoAdaptador: Story = {
	args: {
		panel: "adapter",
		velocidade: null,
		rpm: null,
		coolant: null,
		borboleta: null,
		map: null,
		tensao: null,
		uptime: "12000",
		btSearchStopped: false,
	},
};

export const Conexao: Story = {
	args: {
		tab: "conexao",
		panel: "connected",
		velocidade: "87",
		rpm: "2450",
		coolant: "91",
		borboleta: "18",
		map: "35",
		tensao: "13.9",
		uptime: "184500",
		btSearchStopped: true,
	},
};

export const Ajustes: Story = {
	args: {
		tab: "ajustes",
		panel: "connected",
		velocidade: "87",
		rpm: "2450",
		coolant: "91",
		borboleta: "18",
		map: "35",
		tensao: "13.9",
		uptime: "184500",
		btSearchStopped: false,
	},
};
