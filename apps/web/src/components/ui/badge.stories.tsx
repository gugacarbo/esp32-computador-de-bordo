import type { Meta, StoryObj } from "@storybook/react-vite";
import { Badge } from "./badge";

const meta = {
	title: "UI/Badge",
	component: Badge,
	tags: ["autodocs"],
	args: { children: "Conectado" },
	argTypes: {
		tone: {
			description: "Tom semântico do status.",
			control: "select",
			options: ["ok", "warn", "error", "muted"],
		},
		children: {
			description: "Texto exibido dentro da etiqueta.",
			control: "text",
		},
	},
} satisfies Meta<typeof Badge>;

export default meta;
type Story = StoryObj<typeof meta>;

/** Estado padrão: tom neutro (`muted`). */
export const Padrao: Story = { args: { tone: "muted", children: "Inativo" } };

/** Status saudável, como conexão estabelecida. */
export const Ok: Story = { args: { tone: "ok" } };

/** Status que pede atenção, sem ser falha. */
export const Atencao: Story = { args: { tone: "warn", children: "Atenção" } };

/** Status de falha. */
export const Falha: Story = { args: { tone: "error", children: "Falha" } };
