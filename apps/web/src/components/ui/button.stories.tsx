import type { Meta, StoryObj } from "@storybook/react-vite";
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

export const Primaria: Story = { args: { variant: "primary" } };
export const Secundaria: Story = {
	args: { variant: "secondary", children: "Cancelar" },
};
export const Fantasma: Story = {
	args: { variant: "ghost", children: "Detalhes" },
};
export const Desabilitada: Story = {
	args: { variant: "primary", disabled: true },
};
