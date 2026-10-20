import { type ReactElement, useState } from "react";
import { requestObdReconnect } from "../lib/obd";
import { Alert } from "./ui/alert";
import { Button } from "./ui/button";

interface ScannerReconnectProps {
	/** Firmware desistiu de buscar o scanner BT (envelope.btSearchStopped). */
	searchStopped: boolean;
}

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
			className="flex flex-col gap-2 rounded-lg border border-status-warn/40 bg-status-warn/10 px-3 py-2 text-sm text-status-warn sm:flex-row sm:items-center sm:justify-between"
		>
			<p className="m-0 min-w-0 break-words">
				Busca pelo scanner pausada após várias tentativas — o Bluetooth fica
				livre para o Wi‑Fi.
			</p>
			<Button
				variant="secondary"
				onClick={() => void handleClick()}
				disabled={pending}
				className="w-full border-status-warn/50 bg-status-warn/10 text-status-warn hover:bg-status-warn/20 sm:w-auto"
			>
				{pending ? "Procurando…" : "Procurar scanner"}
			</Button>
			{error !== null && (
				<Alert
					tone="error"
					showPrefix={false}
					role="alert"
					className="break-words sm:basis-full"
				>
					{error}
				</Alert>
			)}
		</section>
	);
}
