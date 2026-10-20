import {
	type FormEvent,
	type ReactElement,
	useCallback,
	useEffect,
	useState,
} from "react";
import { cn } from "../lib/utils";
import {
	describeWifiError,
	parseWifiActionResponse,
	parseWifiStatus,
	validateWifiForm,
	WIFI_PATH,
	type WifiStatus,
	wifiModeLabel,
} from "../lib/wifi";

const FETCH_TIMEOUT_MS = 5000;

const NETWORK_ERROR =
	"Sem resposta da placa. Se ela estiver reiniciando, aguarde e confira o hotspot; caso contrário, tente de novo.";

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

/** Chamada a /api/wifi com timeout. Lança erro somente em falha de rede. */
async function requestWifi(
	method: "GET" | "POST" | "DELETE",
	payload?: Record<string, string>,
): Promise<ApiResult> {
	const ctrl = new AbortController();
	const timeout = window.setTimeout(() => ctrl.abort(), FETCH_TIMEOUT_MS);
	try {
		const response = await fetch(WIFI_PATH, {
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

async function readStatus(): Promise<
	{ status: WifiStatus; error: null } | { status: null; error: string }
> {
	try {
		const result = await requestWifi("GET");
		if (result.status !== 200) {
			return {
				status: null,
				error: describeWifiError(result.status, result.body),
			};
		}
		const parsed = parseWifiStatus(result.body);
		if (parsed === null) {
			return {
				status: null,
				error: "Resposta do Wi-Fi em formato inesperado.",
			};
		}
		return { status: parsed, error: null };
	} catch {
		return {
			status: null,
			error: "Falha ao obter o estado do Wi-Fi. Tente Atualizar.",
		};
	}
}

function savedMessage(ssid: string): string {
	return `Salvo. A placa vai reiniciar e tentar o hotspot ${ssid}. Conecte o celular ao hotspot e abra http://<faixa do hotspot>.50. O IP fica entre .50 e .59 na faixa do hotspot e aparece na lista de dispositivos conectados como "onix".`;
}

/**
 * Seção recolhida "Rede Wi-Fi": mostra o estado da conexão com o hotspot e
 * permite cadastrar ou esquecer o hotspot. Ao abrir, consulta GET /api/wifi
 * uma vez; "Atualizar" repete a consulta. Salvar ou esquecer reinicia a placa.
 */
export function WifiPanel(): ReactElement {
	const [open, setOpen] = useState(false);
	const [status, setStatus] = useState<WifiStatus | null>(null);
	const [statusError, setStatusError] = useState<string | null>(null);
	const [loading, setLoading] = useState(false);

	const [ssid, setSsid] = useState("");
	const [password, setPassword] = useState("");
	const [busy, setBusy] = useState(false);
	const [feedback, setFeedback] = useState<Feedback | null>(null);

	const refreshStatus = useCallback(async (): Promise<void> => {
		setLoading(true);
		const result = await readStatus();
		if (result.status !== null) {
			setStatus(result.status);
			setStatusError(null);
		} else {
			setStatusError(result.error);
		}
		setLoading(false);
	}, []);

	useEffect(() => {
		if (open) void refreshStatus();
	}, [open, refreshStatus]);

	async function handleSave(event: FormEvent<HTMLFormElement>): Promise<void> {
		event.preventDefault();
		const formError = validateWifiForm({ ssid, password });
		if (formError !== null) {
			setFeedback({ tone: "error", text: formError });
			return;
		}
		setBusy(true);
		setFeedback(null);
		try {
			const result = await requestWifi("POST", { ssid, password });
			if (
				result.status === 200 &&
				parseWifiActionResponse(result.body) !== null
			) {
				setFeedback({ tone: "ok", text: savedMessage(ssid) });
				setPassword("");
			} else {
				setFeedback({
					tone: "error",
					text: describeWifiError(result.status, result.body),
				});
			}
		} catch {
			setFeedback({ tone: "error", text: NETWORK_ERROR });
		} finally {
			setBusy(false);
		}
	}

	async function handleForget(): Promise<void> {
		if (
			!window.confirm(
				"Esquecer o hotspot cadastrado? A placa vai reiniciar e usar a rede própria (Onix-MVP).",
			)
		) {
			return;
		}
		setBusy(true);
		setFeedback(null);
		try {
			const result = await requestWifi("DELETE");
			if (
				result.status === 200 &&
				parseWifiActionResponse(result.body) !== null
			) {
				setFeedback({
					tone: "ok",
					text: "Hotspot esquecido. A placa vai reiniciar e usar a rede própria (Onix-MVP).",
				});
			} else {
				setFeedback({
					tone: "error",
					text: describeWifiError(result.status, result.body),
				});
			}
		} catch {
			setFeedback({ tone: "error", text: NETWORK_ERROR });
		} finally {
			setBusy(false);
		}
	}

	return (
		<details
			id="wifi"
			className="rounded-lg border border-slate-700/60 bg-slate-900 px-3 py-2 text-sm"
			onToggle={(event) => setOpen(event.currentTarget.open)}
		>
			<summary className="cursor-pointer select-none font-semibold text-slate-300">
				Rede Wi-Fi
			</summary>
			<div className="mt-3 flex flex-col gap-4">
				<section
					className="flex flex-col gap-2"
					aria-labelledby="wifi-status-title"
				>
					<div className="flex flex-wrap items-center justify-between gap-2">
						<h2
							id="wifi-status-title"
							className="m-0 text-sm font-semibold text-slate-200"
						>
							Estado atual
						</h2>
						<button
							type="button"
							onClick={() => void refreshStatus()}
							disabled={loading || busy}
							className={cn(
								BUTTON_BASE,
								"border-slate-600 bg-slate-800 text-slate-200 hover:bg-slate-700",
							)}
						>
							{loading ? "Atualizando…" : "Atualizar"}
						</button>
					</div>
					{statusError !== null && (
						<p role="alert" className="m-0 break-words text-rose-300">
							{statusError}
						</p>
					)}
					{status === null && statusError === null && (
						<p className="m-0 text-slate-400">Carregando estado do Wi-Fi…</p>
					)}
					{status !== null && (
						<dl className="m-0 grid grid-cols-[auto_1fr] gap-x-3 gap-y-1">
							<dt className="text-slate-400">Modo</dt>
							<dd className="m-0 min-w-0 break-words text-slate-100">
								{wifiModeLabel(status.mode)}
							</dd>
							<dt className="text-slate-400">SSID cadastrado</dt>
							<dd className="m-0 min-w-0 break-words text-slate-100">
								{status.ssid || "Nenhum cadastrado"}
							</dd>
							<dt className="text-slate-400">IP</dt>
							<dd className="m-0 min-w-0 break-words tabular-nums text-slate-100">
								{status.ip || "—"}
							</dd>
						</dl>
					)}
				</section>

				<form
					className="flex flex-col gap-3"
					onSubmit={(event) => void handleSave(event)}
					noValidate
				>
					<div className="flex flex-col gap-1">
						<label htmlFor="wifi-ssid" className="font-semibold text-slate-200">
							Nome do hotspot (SSID)
						</label>
						<input
							id="wifi-ssid"
							type="text"
							autoComplete="off"
							autoCapitalize="none"
							spellCheck={false}
							aria-describedby="wifi-ssid-hint"
							value={ssid}
							onChange={(event) => setSsid(event.target.value)}
							className={INPUT_CLASS}
						/>
						<p id="wifi-ssid-hint" className="m-0 text-xs text-slate-400">
							De 1 a 32 bytes em UTF-8.
						</p>
					</div>

					<div className="flex flex-col gap-1">
						<label
							htmlFor="wifi-password"
							className="font-semibold text-slate-200"
						>
							Senha do hotspot
						</label>
						<input
							id="wifi-password"
							type="password"
							autoComplete="new-password"
							aria-describedby="wifi-password-hint"
							value={password}
							onChange={(event) => setPassword(event.target.value)}
							className={INPUT_CLASS}
						/>
						<p id="wifi-password-hint" className="m-0 text-xs text-slate-400">
							De 8 a 63 bytes. Deixe vazia apenas se o hotspot não tiver senha.
						</p>
					</div>

					<div className="flex flex-col gap-2 sm:flex-row">
						<button
							type="submit"
							disabled={busy}
							className={cn(
								BUTTON_BASE,
								"border-emerald-400/40 bg-emerald-400/10 text-emerald-300 hover:bg-emerald-400/20",
							)}
						>
							{busy ? "Salvando…" : "Salvar e reiniciar"}
						</button>
						<button
							type="button"
							onClick={() => void handleForget()}
							disabled={busy || status?.configured === false}
							className={cn(
								BUTTON_BASE,
								"border-rose-400/40 bg-rose-400/10 text-rose-300 hover:bg-rose-400/20",
							)}
						>
							Esquecer hotspot
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
