import type { Meta, StoryObj } from "@storybook/react-vite";
import { Badge } from "./badge";
import { Card, CardContent, CardHeader, CardTitle } from "./card";

const meta = {
	title: "UI/Card",
	component: Card,
	tags: ["autodocs"],
	argTypes: {
		className: {
			description: "Classes Tailwind extras aplicadas ao contêiner.",
			control: "text",
		},
	},
} satisfies Meta<typeof Card>;

export default meta;
type Story = StoryObj<typeof meta>;

/** Estado padrão: cabeçalho com título e conteúdo. */
export const Padrao: Story = {
	render: (args) => (
		<Card {...args}>
			<CardHeader>
				<CardTitle>Motor</CardTitle>
			</CardHeader>
			<CardContent>
				<p>Temperatura: 92 °C</p>
			</CardContent>
		</Card>
	),
};

/** Estado vazio: sem conteúdo ainda para exibir. */
export const Vazio: Story = {
	render: (args) => (
		<Card {...args}>
			<CardHeader>
				<CardTitle>Viagens</CardTitle>
			</CardHeader>
			<CardContent>
				<p>Nenhuma viagem registrada.</p>
			</CardContent>
		</Card>
	),
};

/** Estado carregando: dados ainda não chegaram. */
export const Carregando: Story = {
	render: (args) => (
		<Card {...args}>
			<CardHeader>
				<CardTitle>OBD</CardTitle>
			</CardHeader>
			<CardContent>
				<p aria-busy="true">Carregando dados…</p>
			</CardContent>
		</Card>
	),
};

/** Estado de falha: o cartão mostra o erro com um `Badge` de tom `error`. */
export const Falha: Story = {
	render: (args) => (
		<Card {...args}>
			<CardHeader>
				<CardTitle>Bluetooth</CardTitle>
			</CardHeader>
			<CardContent>
				<Badge tone="error">Falha</Badge>
			</CardContent>
		</Card>
	),
};
