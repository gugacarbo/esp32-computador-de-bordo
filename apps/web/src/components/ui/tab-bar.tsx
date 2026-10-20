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
							"flex min-h-touch flex-1 basis-0 items-center justify-center",
							"text-xs font-semibold uppercase tracking-widest",
							"focus-visible:outline focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-accent",
							active ? "text-accent" : "text-fg-muted hover:text-fg",
						)}
					>
						{TAB_LABELS[id]}
					</button>
				);
			})}
		</div>
	);
}
