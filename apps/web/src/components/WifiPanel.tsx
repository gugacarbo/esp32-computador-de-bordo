import {
	type FormEvent,
	type ReactElement,
	useCallback,
	useEffect,
	useState,
} from "react";
import {
	describeWifiError,
	parseWifiActionResponse,
	parseWifiStatus,
	validateWifiForm,
	WIFI_PATH,
	type WifiStatus,
	wifiModeLabel,
} from "../lib/wifi";
import { Alert } from "./ui/alert";
import { Badge } from "./ui/badge";
import { Button } from "./ui/button";
import { Field } from "./ui/field";

const FETCH_TIMEOUT_MS = 5000;

const NETWORK_ERROR =
	"Sem resposta da placa. Se ela estiver reiniciando, aguarde e confira o hotspot; caso contrário, tente de novo.";

const SAVE_BUTTON_CLASS =
	"border-status-ok/40 bg-status-ok/10 text-status-ok hover:bg-status-ok/20";

const FORGET_BUTTON_CLASS =
	"border-status-error/40 bg-status-error/10 text-status-error hover:bg-status-error/20";

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
		void refreshStatus();
	}, [refreshStatus]);

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

	const triggerStatus =
		status === null
			? statusError === null
				? { label: "Verificando", tone: "muted" as const }
				: { label: "Indisponível", tone: "error" as const }
			: status.mode === "station"
				? { label: "Conectado", tone: "ok" as const }
				: status.mode === "connecting"
					? { label: "Conectando", tone: "warn" as const }
					: status.mode === "station-lost"
						? { label: "Reconectando", tone: "warn" as const }
						: { label: "Ponto de acesso", tone: "muted" as const };

	return (
		<details
			id="wifi"
			className="group rounded-lg border border-line/60 bg-surface-raised px-3 py-2 text-sm"
			onToggle={(event) => setOpen(event.currentTarget.open)}
		>
			<summary className="flex cursor-pointer items-center justify-between gap-2 font-semibold text-fg">
				<span className="flex items-center gap-1.5">
					<span
						aria-hidden="true"
						className="transition-transform group-open:rotate-90"
					>
						▸
					</span>
					Rede Wi-Fi
				</span>
				<Badge tone={triggerStatus.tone}>{triggerStatus.label}</Badge>
			</summary>
			<div className="mt-3 flex flex-col gap-4">
				<section
					className="flex flex-col gap-2"
					aria-labelledby="wifi-status-title"
				>
					<div className="flex flex-wrap items-center justify-between gap-2">
						<h2
							id="wifi-status-title"
							className="m-0 text-sm font-semibold text-fg"
						>
							Estado atual
						</h2>
						<Button
							variant="secondary"
							onClick={() => void refreshStatus()}
							disabled={loading || busy}
						>
							{loading ? "Atualizando…" : "Atualizar"}
						</Button>
					</div>
					{statusError !== null && (
						<Alert tone="error" showPrefix={false} role="alert">
							{statusError}
						</Alert>
					)}
					{status === null && statusError === null && (
						<p className="m-0 text-fg-muted">Carregando estado do Wi-Fi…</p>
					)}
					{status !== null && (
						<dl className="m-0 grid grid-cols-[auto_1fr] gap-x-3 gap-y-1">
							<dt className="text-fg-muted">Modo</dt>
							<dd className="m-0 min-w-0 break-words text-fg">
								{wifiModeLabel(status.mode)}
							</dd>
							<dt className="text-fg-muted">SSID cadastrado</dt>
							<dd className="m-0 min-w-0 break-words text-fg">
								{status.ssid || "Nenhum cadastrado"}
							</dd>
							<dt className="text-fg-muted">IP</dt>
							<dd className="m-0 min-w-0 break-words tabular-nums text-fg">
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
					<Field
						label="Nome do hotspot (SSID)"
						id="wifi-ssid"
						hint="De 1 a 32 bytes em UTF-8."
						hintId="wifi-ssid-hint"
						type="text"
						autoComplete="off"
						autoCapitalize="none"
						spellCheck={false}
						value={ssid}
						onChange={(event) => setSsid(event.target.value)}
					/>

					<Field
						label="Senha do hotspot"
						id="wifi-password"
						hint="De 8 a 63 bytes. Deixe vazia apenas se o hotspot não tiver senha."
						hintId="wifi-password-hint"
						type="password"
						autoComplete="new-password"
						value={password}
						onChange={(event) => setPassword(event.target.value)}
					/>

					<div className="flex flex-col gap-2 sm:flex-row">
						<Button
							type="submit"
							variant="secondary"
							disabled={busy}
							className={SAVE_BUTTON_CLASS}
						>
							{busy ? "Salvando…" : "Salvar e reiniciar"}
						</Button>
						<Button
							type="button"
							variant="secondary"
							onClick={() => void handleForget()}
							disabled={busy || status?.configured === false}
							className={FORGET_BUTTON_CLASS}
						>
							Esquecer hotspot
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
