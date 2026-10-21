import type { ReactElement } from "react";
import { TAB_LABELS, TABS, type TabId } from "../../lib/tabs";
import { cn } from "../../lib/utils";

/** Propriedades do {@link TabBar}. */
export interface TabBarProps {
	/** Aba ativa; controlado pelo estado local do painel (sem router). */
	value: TabId;
	/** Chamado com a aba tocada. Opcional para exibição estática (stories). */
	onChange?: (value: TabId) => void;
}

function TabIcon({ id }: { id: TabId }): ReactElement {
	const common = {
		"aria-hidden": true,
		fill: "none",
		stroke: "currentColor",
		strokeLinecap: "round" as const,
		strokeLinejoin: "round" as const,
		strokeWidth: 1.8,
		viewBox: "0 0 24 24",
		className: "size-4 shrink-0",
	};

	switch (id) {
		case "telemetria":
			return (
				<svg {...common}>
					<path d="M4.9 19a9 9 0 1 1 14.2 0" />
					<path d="m12 13 4-4" />
					<path d="M12 17h.01" />
				</svg>
			);
		case "conexao":
			return (
				<svg {...common}>
					<circle cx="6" cy="7" r="2" />
					<circle cx="18" cy="7" r="2" />
					<circle cx="12" cy="17" r="2" />
					<path d="M8 7h8M7.5 8.5l3.2 6.8m5.8-6.8-3.2 6.8" />
				</svg>
			);
		case "ajustes":
			return (
				<svg {...common}>
					<path d="M4 7h9m4 0h3M4 17h3m4 0h9" />
					<circle cx="15" cy="7" r="2" />
					<circle cx="9" cy="17" r="2" />
				</svg>
			);
	}
}

/**
 * Barra de abas fixa na base do painel (navegação mobile-first). Sem router:
 * a troca é estado local, atrás de um `TabId` do registro `lib/tabs`.
 * Sempre 3 abas: Telemetria, Conexão e Ajustes.
 */
export function TabBar({ value, onChange }: TabBarProps): ReactElement {
	return (
		<div
			role="tablist"
			aria-label="Seções do painel"
			className="flex w-full items-stretch justify-stretch gap-1 border-t border-line bg-surface-raised px-1 pb-[env(safe-area-inset-bottom)]"
		>
			{TABS.map((id) => {
				const active = id === value;
				return (
					<button
						key={id}
						type="button"
						id={`tab-${id}`}
						role="tab"
						aria-selected={active}
						aria-controls={`panel-${id}`}
						onClick={() => onChange?.(id)}
						className={cn(
							"flex min-h-14 flex-1 basis-0 flex-col items-center justify-center gap-1",
							"text-xs font-semibold uppercase tracking-widest",
							"focus-visible:outline focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-accent",
							active ? "text-accent" : "text-fg-muted hover:text-fg",
						)}
					>
						<TabIcon id={id} />
						{TAB_LABELS[id]}
					</button>
				);
			})}
		</div>
	);
}
