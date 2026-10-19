import { type ReactElement, useState } from "react";
import { requestObdReconnect } from "../lib/obd";
import { cn } from "../lib/utils";

interface ScannerReconnectProps {
	/** Firmware desistiu de buscar o scanner BT (envelope.btSearchStopped). */
	searchStopped: boolean;
}

const BUTTON_CLASS =
	"min-h-11 w-full rounded-md border px-4 py-2 text-sm font-semibold sm:w-auto disabled:cursor-not-allowed disabled:opacity-50";

/**
 * Aviso compacto quando a placa parou de procurar o scanner OBD após várias
 * tentativas. O botão pede uma nova busca; sem o flag, não renderiza nada.
 */
export function ScannerReconnect({
	searchStopped,
}: ScannerReconnectProps): ReactElement | null {
	const [pending, setPending] = useState(false);
	const [error, setError] = useState<string | null>(null);

	if (!searchStopped) return null;

	async function handleClick(): Promise<void> {
		setPending(true);
		setError(null);
		try {
			const result = await requestObdReconnect();
			if (!result.ok) {
				setError(result.error ?? "Não foi possível pedir a busca.");
			}
		} finally {
			setPending(false);
		}
	}

	return (
		<section
			aria-label="Busca pelo scanner"
			className="flex flex-col gap-2 rounded-lg border border-amber-300/40 bg-amber-300/10 px-3 py-2 text-sm text-amber-100 sm:flex-row sm:items-center sm:justify-between"
		>
			<p className="m-0 min-w-0 break-words">
				Busca pelo scanner pausada após várias tentativas — o Bluetooth fica
				livre para o Wi‑Fi.
			</p>
			<button
				type="button"
				onClick={() => void handleClick()}
				disabled={pending}
				className={cn(
					BUTTON_CLASS,
					"border-amber-300/50 bg-amber-300/10 text-amber-100 hover:bg-amber-300/20",
				)}
			>
				{pending ? "Procurando…" : "Procurar scanner"}
			</button>
			{error !== null && (
				<p role="alert" className="m-0 break-words text-rose-300 sm:basis-full">
					{error}
				</p>
			)}
		</section>
	);
}
