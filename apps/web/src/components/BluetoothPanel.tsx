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
	type BtConfigStatus,
	type BtDevice,
	describeBtError,
	isScanRunning,
	normalizeMac,
	parseBtConfigStatus,
	scanStateLabel,
	validateBtConfig,
} from "../lib/btconfig";
import { cn } from "../lib/utils";

const FETCH_TIMEOUT_MS = 5000;
const SCAN_POLL_MS = 1000;
const SCAN_MAX_POLLS = 20;
const NETWORK_ERROR = "Sem resposta da placa. Tente de novo.";

const INPUT_CLASS =
	"w-full rounded-md border border-slate-700 bg-slate-950 px-3 py-2 text-base text-slate-100 placeholder:text-slate-500 focus-visible:outline-2 focus-visible:outline-sky-400";

const BUTTON_BASE =
	"min-h-11 rounded-md border px-4 py-2 text-sm font-semibold disabled:cursor-not-allowed disabled:opacity-50";

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

function deviceLabel(device: BtDevice): string {
	const name = device.name || "(sem nome)";
	return device.rssi === null ? name : `${name} · ${device.rssi} dBm`;
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
		const input = { name: name.trim(), pin, mac: normalizeMac(mac) };
		const formError = validateBtConfig(input);
		if (formError !== null) {
			setFeedback({ tone: "error", text: formError });
			return;
		}
		setBusy(true);
		setFeedback(null);
		try {
			const result = await requestBt(BT_CONFIG_PATH, "POST", input);
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
			className="rounded-lg border border-slate-700/60 bg-slate-900 px-3 py-2 text-sm"
			onToggle={(event) => setOpen(event.currentTarget.open)}
		>
			<summary className="cursor-pointer select-none font-semibold text-slate-300">
				Scanner Bluetooth
			</summary>
			<div className="mt-3 flex flex-col gap-4">
				{statusError !== null && (
					<p role="alert" className="m-0 break-words text-rose-300">
						{statusError}
					</p>
				)}
				{status !== null && (
					<dl className="m-0 grid grid-cols-[auto_1fr] gap-x-3 gap-y-1">
						<dt className="text-slate-400">Em uso</dt>
						<dd className="m-0 min-w-0 break-words text-slate-100">
							{status.name || "(sem nome)"}
							{status.mac !== "" && (
								<span className="tabular-nums text-slate-400">
									{" "}
									· {status.mac}
								</span>
							)}
						</dd>
						<dt className="text-slate-400">Origem</dt>
						<dd className="m-0 text-slate-100">
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
							className="m-0 text-sm font-semibold text-slate-200"
						>
							Dispositivos próximos
						</h2>
						<button
							type="button"
							onClick={() => void handleScan()}
							disabled={scanning || busy}
							className={cn(
								BUTTON_BASE,
								"border-sky-400/40 bg-sky-400/10 text-sky-300 hover:bg-sky-400/20",
							)}
						>
							{scanning ? "Buscando…" : "Buscar"}
						</button>
					</div>
					{scanLabel !== "" && (
						<p className="m-0 text-xs text-slate-400" aria-live="polite">
							{scanLabel}
						</p>
					)}
					{!scanning && scanState === "done" && devices.length === 0 && (
						<p className="m-0 text-slate-400">
							Nenhum dispositivo encontrado. Confira se o scanner está ligado.
						</p>
					)}
					{devices.length > 0 && (
						<ul className="m-0 flex list-none flex-col gap-1 p-0">
							{devices.map((device) => (
								<li key={device.mac}>
									<button
										type="button"
										onClick={() => choose(device)}
										className={cn(
											"flex min-h-11 w-full flex-col items-start rounded-md border px-3 py-2 text-left",
											normalizeMac(mac) === device.mac
												? "border-emerald-400/60 bg-emerald-400/10"
												: "border-slate-700 bg-slate-950 hover:bg-slate-800",
										)}
									>
										<span className="break-all text-slate-100">
											{deviceLabel(device)}
										</span>
										<span className="text-xs tabular-nums text-slate-400">
											{device.mac}
										</span>
									</button>
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
					<div className="flex flex-col gap-1">
						<label htmlFor="bt-name" className="font-semibold text-slate-200">
							Nome do scanner
						</label>
						<input
							id="bt-name"
							type="text"
							autoComplete="off"
							autoCapitalize="none"
							spellCheck={false}
							value={name}
							onChange={(event) => setName(event.target.value)}
							className={INPUT_CLASS}
						/>
					</div>
					<div className="flex flex-col gap-1">
						<label htmlFor="bt-mac" className="font-semibold text-slate-200">
							MAC
						</label>
						<input
							id="bt-mac"
							type="text"
							autoComplete="off"
							autoCapitalize="characters"
							spellCheck={false}
							placeholder="AA:BB:CC:DD:EE:FF"
							aria-describedby="bt-mac-hint"
							value={mac}
							onChange={(event) => setMac(event.target.value)}
							className={cn(INPUT_CLASS, "tabular-nums")}
						/>
						<p id="bt-mac-hint" className="m-0 text-xs text-slate-400">
							Com o MAC a placa conecta direto, sem buscar pelo nome (menos
							impacto no Wi-Fi).
						</p>
					</div>
					<div className="flex flex-col gap-1">
						<label htmlFor="bt-pin" className="font-semibold text-slate-200">
							PIN
						</label>
						<input
							id="bt-pin"
							type="text"
							inputMode="numeric"
							autoComplete="off"
							aria-describedby="bt-pin-hint"
							value={pin}
							onChange={(event) => setPin(event.target.value)}
							className={INPUT_CLASS}
						/>
						<p id="bt-pin-hint" className="m-0 text-xs text-slate-400">
							Geralmente 1234 ou 0000.
						</p>
					</div>
					<div className="flex flex-col gap-2 sm:flex-row">
						<button
							type="submit"
							disabled={busy || scanning}
							className={cn(
								BUTTON_BASE,
								"border-emerald-400/40 bg-emerald-400/10 text-emerald-300 hover:bg-emerald-400/20",
							)}
						>
							{busy ? "Salvando…" : "Salvar e conectar"}
						</button>
						<button
							type="button"
							onClick={() => void handleReset()}
							disabled={busy || scanning || status?.custom === false}
							className={cn(
								BUTTON_BASE,
								"border-rose-400/40 bg-rose-400/10 text-rose-300 hover:bg-rose-400/20",
							)}
						>
							Restaurar padrão
						</button>
					</div>
				</form>

				{feedback !== null && (
					<p
						role="alert"
						className={cn(
							"m-0 break-words",
							feedback.tone === "ok" ? "text-emerald-300" : "text-rose-300",
						)}
					>
						{feedback.text}
					</p>
				)}
			</div>
		</details>
	);
}
