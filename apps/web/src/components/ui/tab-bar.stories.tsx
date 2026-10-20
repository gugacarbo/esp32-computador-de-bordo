import type { Meta, StoryObj } from "@storybook/react-vite";
import { type ReactElement, useState } from "react";
import { TAB_LABELS, TABS, type TabId } from "../../lib/tabs";
import { TabBar } from "./tab-bar";

/**
 * Demonstração do par aba/painéis: a barra sempre acompanha elementos com
 * id `panel-<id>` no App, e a regra aria-valid-attr-value do axe exige os
 * alvos de `aria-controls` na página. Aqui os painéis são stubs visuais.
 */
function TabBarDemo(): ReactElement {
	const [value, setValue] = useState<TabId>("telemetria");
	return (
		<div className="flex flex-col gap-2">
			<TabBar value={value} onChange={setValue} />
			{TABS.map((id) => (
				<div
					key={id}
					role="tabpanel"
					id={`panel-${id}`}
					aria-labelledby={`tab-${id}`}
					hidden={id !== value}
					className="rounded-card border border-line bg-surface-raised p-3 text-sm text-fg-muted"
				>
					Conteúdo da aba {TAB_LABELS[id]} no painel.
				</div>
			))}
		</div>
	);
}

const meta = {
	title: "UI/TabBar",
	component: TabBar,
	tags: ["autodocs"],
	args: { value: "telemetria" },
	render: () => <TabBarDemo />,
} satisfies Meta<typeof TabBar>;

export default meta;
type Story = StoryObj<typeof meta>;

/** Estado padrão: Telemetria ativa, com os painéis gêmeos do App. */
export const Padrao: Story = {};
