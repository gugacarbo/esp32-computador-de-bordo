// tabs.ts — registro das abas do painel redesign mobile-first (barra inferior).
// A troca de aba é estado local no App (sem router, bundle único, ADR-0010).

export type TabId = "telemetria" | "conexao" | "ajustes";

/** Abas na ordem da barra inferior: a telemetria é o uso essencial no volante. */
export const TABS: readonly TabId[] = [
	"telemetria",
	"conexao",
	"ajustes",
] as const;

/** Rótulos pt-BR dos botões da barra de abas. */
export const TAB_LABELS: Record<TabId, string> = {
	telemetria: "Telemetria",
	conexao: "Conexão",
	ajustes: "Ajustes",
};

/** Aba inicial do painel: dirigir só precisa dos números. */
export const DEFAULT_TAB_ID: TabId = "telemetria";
