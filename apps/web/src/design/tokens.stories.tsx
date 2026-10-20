import type { Meta, StoryObj } from "@storybook/react-vite";

const cores = [
	{ nome: "surface", classe: "bg-surface" },
	{ nome: "surface-raised", classe: "bg-surface-raised" },
	{ nome: "line", classe: "bg-line" },
	{ nome: "fg", classe: "bg-fg" },
	{ nome: "fg-muted", classe: "bg-fg-muted" },
	{ nome: "accent", classe: "bg-accent" },
	{ nome: "status-ok", classe: "bg-status-ok" },
	{ nome: "status-warn", classe: "bg-status-warn" },
	{ nome: "status-error", classe: "bg-status-error" },
	{ nome: "status-muted", classe: "bg-status-muted" },
] as const;

function Tokens() {
	return (
		<div className="flex flex-col gap-6 bg-surface p-4 font-sans text-fg">
			<section className="flex flex-col gap-3">
				<h2 className="text-lg font-semibold">Cores</h2>
				<div className="grid grid-cols-2 gap-3 sm:grid-cols-3">
					{cores.map((cor) => (
						<div
							key={cor.nome}
							className="flex flex-col gap-2 rounded-card border border-line bg-surface-raised p-3"
						>
							<div className={`h-12 rounded-card ${cor.classe}`} />
							<span className="font-mono text-sm">{cor.nome}</span>
						</div>
					))}
				</div>
			</section>

			<section className="flex flex-col gap-3">
				<h2 className="text-lg font-semibold">Tipografia</h2>
				<p className="text-2xl font-semibold">Velocidade 87 km/h</p>
				<p className="text-base">Corpo de texto, Conectado ao scanner</p>
				<p className="text-sm text-fg-muted">Legenda: última leitura há 2 s</p>
			</section>

			<section className="flex flex-col gap-3">
				<h2 className="text-lg font-semibold">Raio e alvo de toque</h2>
				<div className="flex items-center gap-4">
					<div className="flex h-touch w-touch items-center justify-center rounded-card bg-accent font-semibold text-surface">
						Toque
					</div>
					<div className="size-16 rounded-card border border-line bg-surface-raised" />
				</div>
			</section>
		</div>
	);
}

const meta = {
	title: "Design/Tokens",
	component: Tokens,
	tags: ["autodocs"],
} satisfies Meta<typeof Tokens>;

export default meta;
type Story = StoryObj<typeof meta>;

export const Paleta: Story = {};
