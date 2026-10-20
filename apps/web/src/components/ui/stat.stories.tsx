import type { Meta, StoryObj } from "@storybook/react-vite";
import { Stat } from "./stat";

const meta = {
	title: "UI/Stat",
	component: Stat,
	tags: ["autodocs"],
	args: { label: "Rotação", value: 2450, unit: "rpm" },
	argTypes: {
		value: { control: "text" },
		unit: { control: "text" },
		tone: { control: "select", options: ["ok", "warn", "error", "muted"] },
		label: {
			control: "text",
			description: "Rótulo opcional exibido acima do valor.",
		},
		valueId: {
			control: "text",
			description:
				"Id aplicado apenas ao elemento que contém o valor (sem a unidade).",
		},
		size: {
			control: "inline-radio",
			options: ["md", "lg"],
			description:
				"Tamanho do valor: `lg` (padrão, text-4xl) ou `md` (text-3xl).",
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
export const Ok: Story = { args: { tone: "ok" } };
export const Alerta: Story = {
	args: { label: "Tensão", value: "11.2", unit: "V", tone: "warn" },
};
export const SemLeitura: Story = {
	args: { label: "Temperatura", value: "—", unit: "°C", tone: "muted" },
};
export const Compacto: Story = {
	args: { size: "md", bare: true },
};
export const SemRotulo: Story = {
	args: { label: undefined, value: 2450, unit: "rpm" },
};
export const Falha: Story = {
	args: { label: "MAP", value: "—", unit: "kPa", tone: "error" },
};
