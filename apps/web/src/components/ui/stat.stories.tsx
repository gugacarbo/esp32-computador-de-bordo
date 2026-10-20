import type { Meta, StoryObj } from "@storybook/react-vite";
import type { StatusTone } from "../../lib/envelope";
import { Stat } from "./stat";

const meta = {
	title: "UI/Stat",
	component: Stat,
	tags: ["autodocs"],
	args: {
		label: "Rotação",
		value: 2450,
		unit: "rpm",
		tone: "ok",
		size: "lg",
		bare: false,
		valueId: undefined,
	},
	argTypes: {
		value: {
			control: "text",
			description: "Valor principal; use '—' quando não houver leitura.",
		},
		unit: {
			control: "text",
			description: "Unidade exibida ao lado do valor.",
		},
		tone: {
			control: "select",
			options: ["ok", "warn", "error", "muted"],
			description: "Cor semântica do valor.",
		},
		size: {
			control: "inline-radio",
			options: ["md", "lg", "xl"],
			description:
				"`xl` herói de relance (text-6xl), `lg` padrão (text-4xl) ou `md` (text-3xl).",
		},
		label: {
			control: "text",
			description: "Rótulo exibido acima do valor.",
		},
		valueId: {
			control: "text",
			description:
				"Id aplicado apenas ao elemento que contém o valor (sem a unidade).",
		},
		bare: {
			control: "boolean",
			description:
				"Remove moldura, fundo e espaçamento, para uso dentro de outro cartão.",
		},
	},
} satisfies Meta<typeof Stat>;

export default meta;
type Story = StoryObj<typeof meta>;

export const Padrao: Story = {};

export const HeroVelocidade: Story = {
	args: {
		label: "Velocidade",
		value: 87,
		unit: "km/h",
		size: "xl",
		tone: "ok",
		bare: true,
	},
};

export const SemLeitura: Story = {
	args: {
		label: "Temperatura",
		value: "—",
		unit: "°C",
		tone: "muted",
	},
};

export const Alerta: Story = {
	args: {
		label: "Tensão",
		value: 11.2,
		unit: "V",
		tone: "warn",
	},
};

export const Falha: Story = {
	args: {
		label: "MAP",
		value: "—",
		unit: "kPa",
		tone: "error",
	},
};

export const Compacto: Story = {
	args: {
		label: "RPM",
		value: 2450,
		unit: "rpm",
		size: "md",
		bare: true,
	},
};
