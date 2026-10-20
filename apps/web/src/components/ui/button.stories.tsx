import type { Meta, StoryObj } from "@storybook/react-vite";
import { expect, fn, userEvent, within } from "storybook/test";
import { Button } from "./button";

const meta = {
	title: "UI/Button",
	component: Button,
	tags: ["autodocs"],
	args: { children: "Salvar" },
	argTypes: {
		variant: {
			control: "select",
			options: ["primary", "secondary", "ghost"],
		},
		disabled: { control: "boolean" },
	},
} satisfies Meta<typeof Button>;

export default meta;
type Story = StoryObj<typeof meta>;

export const Primaria: Story = {
	args: { variant: "primary", onClick: fn() },
	play: async ({ args, canvasElement }) => {
		const canvas = within(canvasElement);
		await userEvent.click(canvas.getByRole("button", { name: "Salvar" }));
		await expect(args.onClick).toHaveBeenCalledTimes(1);
	},
};
export const Secundaria: Story = {
	args: { variant: "secondary", children: "Cancelar" },
};
export const Fantasma: Story = {
	args: { variant: "ghost", children: "Detalhes" },
};
export const Desabilitada: Story = {
	args: { variant: "primary", disabled: true, onClick: fn() },
	play: async ({ args, canvasElement }) => {
		const canvas = within(canvasElement);
		const botao = canvas.getByRole("button", { name: "Salvar" });
		await expect(botao).toBeDisabled();
		await userEvent.click(botao, { pointerEventsCheck: 0 });
		await expect(args.onClick).not.toHaveBeenCalled();
	},
};
