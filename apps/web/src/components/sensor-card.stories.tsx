import type { Meta, StoryObj } from "@storybook/react-vite";
import type { ReactElement } from "react";
import type { SensorKey, SensorReading } from "../lib/envelope";
import { SENSOR_UNITS } from "../lib/envelope";
import { SensorCard } from "./SensorCard";

const sensores: { key: SensorKey; title: string }[] = [
	{ key: "speedKph", title: "Velocidade" },
	{ key: "rpm", title: "RPM" },
	{ key: "coolantC", title: "Temperatura do líquido de arrefecimento" },
	{ key: "throttlePercent", title: "Borboleta" },
	{ key: "mapKpa", title: "MAP" },
	{ key: "voltage", title: "Tensão do adaptador" },
];

function lerNumero(valor: string | undefined | null): number | null {
	if (valor === undefined || valor === null || valor === "") return null;
	const n = Number(valor);
	return Number.isFinite(n) ? n : null;
}

type SensorCardDemoArgs = {
	sensor?: SensorKey;
	title?: string;
	valor?: string | null;
	status?: string;
	valid?: boolean;
	offline?: boolean;
};

function SensorCardDemo(args: SensorCardDemoArgs): ReactElement {
	const key: SensorKey = args.sensor ?? "speedKph";
	const unit = SENSOR_UNITS[key];
	const valor = lerNumero(args.valor ?? null);
	const offline = args.offline ?? false;
	const valid = args.valid ?? true;
	const status: string = args.status ?? "ok";

	let reading: SensorReading | null = null;
	if (!offline && valor !== null && valid) {
		reading = {
			value: valor,
			valid: true,
			status: status as SensorReading["status"],
			updatedAtMs: Date.now(),
			ageMs: 55,
		};
	} else if (!offline && valor !== null && !valid) {
		reading = {
			value: valor,
			valid: false,
			status: status as SensorReading["status"],
			updatedAtMs: Date.now(),
			ageMs: 55,
		};
	}

	const titulo =
		args.title ?? sensores.find((s) => s.key === key)?.title ?? key;

	return (
		<SensorCard
			sensor={key}
			title={titulo}
			reading={reading}
			offline={offline}
		/>
	);
}

const meta = {
	title: "UI/SensorCard",
	component: SensorCardDemo,
	tags: ["autodocs"],
	args: {
		sensor: "speedKph",
		title: "Velocidade",
		valor: "87",
		status: "ok",
		valid: true,
		offline: false,
	},
	argTypes: {
		sensor: {
			control: "select",
			options: sensores.map((s) => s.key),
			description: "Chave do sensor (define unidade e formatação).",
		},
		title: {
			control: "text",
			description: "Título exibido no cabeçalho do cartão.",
		},
		valor: {
			control: "text",
			description:
				"Valor da leitura; deixe vazio para simular sem leitura (null).",
		},
		status: {
			control: "select",
			options: [
				"ok",
				"unsupported",
				"stale",
				"timeout",
				"error",
				"unavailable",
			],
			description: "Status da leitura do sensor.",
		},
		valid: {
			control: "boolean",
			description: "Se a leitura é válida (inválido = status diferente de ok).",
		},
		offline: {
			control: "boolean",
			description:
				"Placa sem comunicação; mostra 'Sem dados atuais' e leitura anterior.",
		},
	},
} satisfies Meta<typeof SensorCardDemo>;

export default meta;
type Story = StoryObj<typeof meta>;

export const Padrao: Story = {};

export const SemLeitura: Story = {
	args: {
		sensor: "rpm",
		title: "RPM",
		valor: null,
		status: "ok",
		valid: false,
		offline: false,
	},
};

export const Desatualizado: Story = {
	args: {
		sensor: "coolantC",
		title: "Temperatura",
		valor: "91",
		status: "stale",
		valid: false,
		offline: false,
	},
};

export const FalhaLeitura: Story = {
	args: {
		sensor: "mapKpa",
		title: "MAP",
		valor: null,
		status: "error",
		valid: false,
		offline: false,
	},
};

export const SemDadosAtuais: Story = {
	args: {
		sensor: "voltage",
		title: "Tensão do adaptador",
		valor: "13.9",
		status: "ok",
		valid: true,
		offline: true,
	},
};
