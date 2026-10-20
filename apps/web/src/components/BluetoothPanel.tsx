import {
	type FormEvent,
	type ReactElement,
	useCallback,
	useEffect,
	useRef,
	useState,
} from "react";
import {
	BT_CONFIG_PATH,
	BT_SCAN_PATH,
	type BtConfigInput,
	type BtConfigStatus,
	type BtDevice,
	describeBtError,
	deviceClassLabel,
	isScanRunning,
	normalizeMac,
	parseBtConfigStatus,
	scanStateLabel,
	validateBtConfig,
} from "../lib/btconfig";
import { cn } from "../lib/utils";
import { Alert } from "./ui/alert";
import { Button } from "./ui/button";
import { Field } from "./ui/field";

const FETCH_TIMEOUT_MS = 5000;
const SCAN_POLL_MS = 1000;
const SCAN_MAX_POLLS = 20;
const NETWORK_ERROR = "Sem resposta da placa. Tente de novo.";

interface Feedback {
	tone: "ok" | "error";
	text: string;
}

interface ApiResult {
	status: number;
	body: unknown;
}

async function readJson(response: Response): Promise<unknown> {
	try {
		return JSON.parse(await response.text());
	} catch {
		return null;
	}
}

/** Chamada a /api/obd/* com timeout. Lança erro somente em falha de rede. */
async function requestBt(
	path: string,
	method: "GET" | "POST" | "DELETE",
	payload?: Record<string, string>,
): Promise<ApiResult> {
	const ctrl = new AbortController();
	const timeout = window.setTimeout(() => ctrl.abort(), FETCH_TIMEOUT_MS);
	try {
		const response = await fetch(path, {
			method,
			cache: "no-store",
			signal: ctrl.signal,
			headers:
				payload === undefined
					? undefined
					: { "Content-Type": "application/json" },
			body: payload === undefined ? undefined : JSON.stringify(payload),
		});
		return { status: response.status, body: await readJson(response) };
	} finally {
		window.clearTimeout(timeout);
	}
}

function deviceDetails(device: BtDevice): string {
	const kind = deviceClassLabel(device.cod);
	return device.rssi === null ? kind : `${kind} · ${device.rssi} dBm`;
}

/**
 * Seção recolhida "Scanner Bluetooth" (ADR-0014): mostra o scanner em uso, busca dispositivos
 * próximos para escolher e grava nome, PIN e MAC na placa. Salvar aplica sem reiniciar: a placa
 * reconecta com a configuração nova e retoma a busca, se estava parada.
 */
export function BluetoothPanel(): ReactElement {
	const [open, setOpen] = useState(false);
	const [status, setStatus] = useState<BtConfigStatus | null>(null);
	const [statusError, setStatusError] = useState<string | null>(null);
	const [name, setName] = useState("");
	const [pin, setPin] = useState("");
	const [mac, setMac] = useState("");
	const [busy, setBusy] = useState(false);
	const [scanning, setScanning] = useState(false);
	const [feedback, setFeedback] = useState<Feedback | null>(null);
	const mounted = useRef(true);

	useEffect(() => {
		mounted.current = true;
		return () => {
			mounted.current = false;
		};
	}, []);

	/** Lê o estado; com `fillForm`, copia a configuração em uso para o formulário. */
	const refresh = useCallback(
		async (fillForm: boolean): Promise<BtConfigStatus | null> => {
			try {
				const result = await requestBt(BT_CONFIG_PATH, "GET");
				const parsed =
					result.status === 200 ? parseBtConfigStatus(result.body) : null;
				if (!mounted.current) return parsed;
				if (parsed === null) {
					setStatusError(
						result.status === 200
							? "Resposta do Bluetooth em formato inesperado."
							: describeBtError(result.status, result.body),
					);
					return null;
				}
				setStatus(parsed);
				setStatusError(null);
				if (fillForm) {
					setName(parsed.name);
					setPin(parsed.pin);
					setMac(parsed.mac);
				}
				return parsed;
			} catch {
				if (mounted.current) setStatusError(NETWORK_ERROR);
				return null;
			}
		},
		[],
	);

	useEffect(() => {
		if (open) void refresh(true);
	}, [open, refresh]);

	async function handleScan(): Promise<void> {
		setScanning(true);
		setFeedback(null);
		try {
			const started = await requestBt(BT_SCAN_PATH, "POST");
			if (started.status !== 202) {
				setFeedback({
					tone: "error",
					text: describeBtError(started.status, started.body),
				});
				return;
			}
			// A busca roda na task OBD (~5 s): consulta o estado até sair de pending/scanning.
			for (let poll = 0; poll < SCAN_MAX_POLLS; poll += 1) {
				await new Promise((resolve) =>
					window.setTimeout(resolve, SCAN_POLL_MS),
				);
				if (!mounted.current) return;
				const current = await refresh(false);
				if (current !== null && !isScanRunning(current.scan.state)) return;
			}
			setFeedback({
				tone: "error",
				text: "A busca demorou demais. Tente de novo.",
			});
		} catch {
			setFeedback({ tone: "error", text: NETWORK_ERROR });
		} finally {
			if (mounted.current) setScanning(false);
		}
	}

	function choose(device: BtDevice): void {
		setMac(device.mac);
		if (device.name !== "") setName(device.name);
		setFeedback(null);
	}

	async function handleSave(event: FormEvent<HTMLFormElement>): Promise<void> {
		event.preventDefault();
		await saveConfig({ name: name.trim(), pin, mac: normalizeMac(mac) });
	}

	/** "Usar" na lista: grava o MAC do dispositivo (e o nome, se houver) mantendo o PIN do formulário. */
	async function applyDevice(device: BtDevice): Promise<void> {
		choose(device);
		await saveConfig({
			name: device.name || name.trim(),
			pin,
			mac: device.mac,
		});
	}

	async function saveConfig(input: BtConfigInput): Promise<void> {
		const formError = validateBtConfig(input);
		if (formError !== null) {
			setFeedback({ tone: "error", text: formError });
			return;
		}
		setBusy(true);
		setFeedback(null);
		try {
			const result = await requestBt(BT_CONFIG_PATH, "POST", { ...input });
			if (result.status === 200) {
				setFeedback({
					tone: "ok",
					text: "Salvo. A placa vai conectar ao scanner com a configuração nova.",
				});
				await refresh(true);
			} else {
				setFeedback({
					tone: "error",
					text: describeBtError(result.status, result.body),
				});
			}
		} catch {
			setFeedback({ tone: "error", text: NETWORK_ERROR });
		} finally {
			setBusy(false);
		}
	}

	async function handleReset(): Promise<void> {
		if (!window.confirm("Voltar ao scanner padrão do firmware?")) return;
		setBusy(true);
		setFeedback(null);
		try {
			const result = await requestBt(BT_CONFIG_PATH, "DELETE");
			if (result.status === 200) {
				setFeedback({ tone: "ok", text: "Configuração padrão restaurada." });
				await refresh(true);
			} else {
				setFeedback({
					tone: "error",
					text: describeBtError(result.status, result.body),
				});
			}
		} catch {
			setFeedback({ tone: "error", text: NETWORK_ERROR });
		} finally {
			setBusy(false);
		}
	}

	const scanState = status?.scan.state ?? "idle";
	const scanLabel = scanning
		? scanStateLabel("scanning")
		: scanStateLabel(scanState);
	const devices = status?.scan.devices ?? [];

	return (
		<details
			id="bluetooth"
			className="rounded-card border border-line bg-surface-raised px-3 py-2 text-sm"
			onToggle={(event) => setOpen(event.currentTarget.open)}
		>
			<summary className="cursor-pointer select-none font-semibold text-fg">
				Scanner Bluetooth
			</summary>
			<div className="mt-3 flex flex-col gap-4">
				{statusError !== null && (
					<Alert tone="error" showPrefix={false} role="alert">
						{statusError}
					</Alert>
				)}
				{status !== null && (
					<dl className="m-0 grid grid-cols-[auto_1fr] gap-x-3 gap-y-1">
						<dt className="text-fg-muted">Em uso</dt>
						<dd className="m-0 min-w-0 break-words text-fg">
							{status.name || "(sem nome)"}
							{status.mac !== "" && (
								<span className="tabular-nums text-fg-muted">
									{" "}
									· {status.mac}
								</span>
							)}
						</dd>
						<dt className="text-fg-muted">Origem</dt>
						<dd className="m-0 text-fg">
							{status.custom ? "Configurado pelo painel" : "Padrão do firmware"}
						</dd>
					</dl>
				)}

				<section
					className="flex flex-col gap-2"
					aria-labelledby="bt-scan-title"
				>
					<div className="flex flex-wrap items-center justify-between gap-2">
						<h2
							id="bt-scan-title"
							className="m-0 text-sm font-semibold text-fg"
						>
							Dispositivos próximos
						</h2>
						<Button
							variant="secondary"
							onClick={() => void handleScan()}
							disabled={scanning || busy}
							className="border-accent/40 bg-accent/10 text-accent hover:bg-accent/20"
						>
							{scanning ? "Buscando…" : "Buscar"}
						</Button>
					</div>
					{scanLabel !== "" && (
						<p className="m-0 text-xs text-fg-muted" aria-live="polite">
							{scanLabel}
						</p>
					)}
					{!scanning && scanState === "done" && devices.length === 0 && (
						<p className="m-0 text-fg-muted">
							Nenhum dispositivo encontrado. Confira se o scanner está ligado.
						</p>
					)}
					{(scanState === "done" || devices.length > 0) && (
						<p className="m-0 text-xs text-fg-muted">
							Aparecem só aparelhos Bluetooth Classic visíveis: dispositivos só
							BLE (incompatíveis com o ELM327 SPP) e celulares fora da tela de
							Bluetooth não respondem à busca.
						</p>
					)}
					{devices.length > 0 && (
						<ul className="m-0 flex list-none flex-col gap-1 p-0">
							{devices.map((device) => (
								<li key={device.mac} className="flex items-stretch gap-1">
									<Button
										type="button"
										variant="secondary"
										onClick={() => choose(device)}
										aria-label={`Preencher o formulário com ${device.name || device.mac}`}
										className={cn(
											"min-w-0 flex-1 flex-col items-start justify-start gap-0 px-3 py-2 text-left font-normal",
											normalizeMac(mac) === device.mac
												? "border-status-ok/60 bg-status-ok/10"
												: "border-line bg-surface hover:bg-line",
										)}
									>
										<span className="break-all text-fg">
											{device.name || "(sem nome)"}
										</span>
										<span className="text-xs text-fg-muted">
											{deviceDetails(device)}
										</span>
										<span className="text-xs tabular-nums text-fg-muted">
											{device.mac}
										</span>
									</Button>
									<Button
										type="button"
										variant="secondary"
										onClick={() => void applyDevice(device)}
										disabled={busy || scanning}
										aria-label={`Usar ${device.name || device.mac} como scanner`}
										className="shrink-0 border-status-ok/40 bg-status-ok/10 text-status-ok hover:bg-status-ok/20"
									>
										Usar
									</Button>
								</li>
							))}
						</ul>
					)}
				</section>

				<form
					className="flex flex-col gap-3"
					onSubmit={(event) => void handleSave(event)}
					noValidate
				>
					<Field
						id="bt-name"
						label="Nome do scanner"
						type="text"
						autoComplete="off"
						autoCapitalize="none"
						spellCheck={false}
						value={name}
						onChange={(event) => setName(event.target.value)}
					/>
					<Field
						id="bt-mac"
						label="MAC"
						type="text"
						autoComplete="off"
						autoCapitalize="characters"
						spellCheck={false}
						placeholder="AA:BB:CC:DD:EE:FF"
						hint="Com o MAC a placa conecta direto, sem buscar pelo nome (menos impacto no Wi-Fi)."
						hintId="bt-mac-hint"
						className="tabular-nums"
						value={mac}
						onChange={(event) => setMac(event.target.value)}
					/>
					<Field
						id="bt-pin"
						label="PIN"
						type="text"
						inputMode="numeric"
						autoComplete="off"
						hint="Geralmente 1234 ou 0000."
						hintId="bt-pin-hint"
						value={pin}
						onChange={(event) => setPin(event.target.value)}
					/>
					<div className="flex flex-col gap-2 sm:flex-row">
						<Button
							type="submit"
							variant="secondary"
							disabled={busy || scanning}
							className="border-status-ok/40 bg-status-ok/10 text-status-ok hover:bg-status-ok/20"
						>
							{busy ? "Salvando…" : "Salvar e conectar"}
						</Button>
						<Button
							variant="secondary"
							onClick={() => void handleReset()}
							disabled={busy || scanning || status?.custom === false}
							className="border-status-error/40 bg-status-error/10 text-status-error hover:bg-status-error/20"
						>
							Restaurar padrão
						</Button>
					</div>
				</form>

				{feedback !== null && (
					<Alert tone={feedback.tone} showPrefix={false} role="alert">
						{feedback.text}
					</Alert>
				)}
			</div>
		</details>
	);
}
