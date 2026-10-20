import type { Meta, StoryObj } from "@storybook/react-vite";
import { expect, userEvent, within } from "storybook/test";
import { Field } from "./field";

const meta = {
	title: "UI/Field",
	component: Field,
	tags: ["autodocs"],
	args: { label: "Nome da rede", placeholder: "Ex.: Onix-MVP" },
	argTypes: {
		hint: { control: "text" },
		hintId: {
			control: "text",
			description:
				"Id do parágrafo de ajuda; por padrão `<id>-ajuda`. Use para manter um id estável.",
		},
		error: { control: "text" },
		disabled: { control: "boolean" },
	},
} satisfies Meta<typeof Field>;

export default meta;
type Story = StoryObj<typeof meta>;

export const Padrao: Story = {
	play: async ({ canvasElement }) => {
		const canvas = within(canvasElement);
		const campo = canvas.getByLabelText("Nome da rede");
		await userEvent.type(campo, "Onix");
		await expect(campo).toHaveValue("Onix");
	},
};
export const Ajuda: Story = {
	args: { hint: "Use até 32 caracteres." },
	play: async ({ canvasElement }) => {
		const canvas = within(canvasElement);
		const campo = canvas.getByLabelText("Nome da rede");
		await expect(campo).toHaveAccessibleDescription("Use até 32 caracteres.");
	},
};
export const IdDaAjudaPersonalizado: Story = {
	args: { hint: "Use até 32 caracteres.", hintId: "ajuda-rede" },
};
export const ComErro: Story = {
	args: { error: "Informe o nome da rede." },
	play: async ({ canvasElement }) => {
		const canvas = within(canvasElement);
		const campo = canvas.getByLabelText("Nome da rede");
		await expect(campo).toHaveAttribute("aria-invalid", "true");
		await expect(campo).toHaveAccessibleDescription(/Informe o nome da rede\./);
	},
};
export const Desabilitado: Story = {
	args: { disabled: true, defaultValue: "Onix-MVP" },
};
