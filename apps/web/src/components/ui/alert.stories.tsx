import type { Meta, StoryObj } from "@storybook/react-vite";
import { expect, within } from "storybook/test";
import { Alert } from "./alert";

const meta = {
	title: "UI/Alert",
	component: Alert,
	tags: ["autodocs"],
	args: { children: "Mensagem de estado do sistema." },
	argTypes: {
		tone: { control: "select", options: ["ok", "warn", "error", "muted"] },
		title: { control: "text" },
		showPrefix: {
			control: "boolean",
			description:
				'Exibe o prefixo textual do tom (ex.: "Erro:"). Desligue quando o texto já deixar o estado explícito.',
		},
	},
} satisfies Meta<typeof Alert>;

export default meta;
type Story = StoryObj<typeof meta>;

export const Info: Story = {
	play: async ({ canvasElement }) => {
		const canvas = within(canvasElement);
		await expect(canvas.getByRole("status")).toHaveTextContent(
			/Info:\s*Mensagem de estado do sistema\./,
		);
	},
};
export const Ok: Story = {
	args: { tone: "ok", children: "Scanner Bluetooth configurado." },
	play: async ({ canvasElement }) => {
		const canvas = within(canvasElement);
		await expect(canvas.getByRole("status")).toHaveTextContent(
			/OK:\s*Scanner Bluetooth configurado\./,
		);
	},
};
export const Atencao: Story = {
	args: { tone: "warn", children: "Sensor de tensão desatualizado." },
	play: async ({ canvasElement }) => {
		const canvas = within(canvasElement);
		await expect(canvas.getByRole("alert")).toHaveTextContent(
			/Atenção:\s*Sensor de tensão desatualizado\./,
		);
	},
};
export const SemPrefixo: Story = {
	args: {
		tone: "error",
		showPrefix: false,
		children: "Falha ao salvar: a placa não respondeu.",
	},
	play: async ({ canvasElement }) => {
		const canvas = within(canvasElement);
		const alerta = canvas.getByRole("alert");
		await expect(alerta).toHaveTextContent(
			/Falha ao salvar: a placa não respondeu\./,
		);
		await expect(alerta).not.toHaveTextContent("Erro:");
	},
};
export const Falha: Story = {
	args: {
		tone: "error",
		title: "Sem comunicação com o ESP32",
		children: "Verifique a conexão Wi-Fi da placa.",
	},
	play: async ({ canvasElement }) => {
		const canvas = within(canvasElement);
		await expect(canvas.getByRole("alert")).toHaveTextContent(
			/Erro:\s*Sem comunicação com o ESP32/,
		);
	},
};
