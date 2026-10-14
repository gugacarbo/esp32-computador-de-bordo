// dashboard-server.ts — fixture Node usada SOMENTE pelos testes Playwright da SPEC-0003.
// Serve o HTML real extraído de apps/firmware/include/dashboard.h e um `GET /api/data` programável,
// para exercitar polling, erro HTTP, JSON inválido, resposta lenta e recuperação sem hardware.
// Implementado sobre node:http porque o runner Playwright desta repo executa em Node (o probe
// registrou Bun.serve indisponível); roda igualmente sob Bun. Não é servidor de produção
// nem substitui o WebServer do ESP32.
import {
	createServer,
	type IncomingMessage,
	type Server,
	type ServerResponse,
} from "node:http";
import type { AddressInfo } from "node:net";
import { extractDashboardHtml } from "./dashboard-source";

/** Plano de resposta para uma requisição de dados. */
export interface DataPlan {
	status?: number;
	body?: string;
	contentType?: string;
	delayMs?: number;
}

export interface RequestRecord {
	path: string;
	method: string;
	status: number;
	startedAt: number;
	endedAt: number;
	concurrentAtStart: number;
}

const DEFAULT_CONTENT_TYPE = "application/json";

export class DashboardServer {
	readonly html: string;
	readonly origin: string;
	readonly requests: RequestRecord[] = [];
	readonly paths: string[] = [];
	private readonly instance: Server;
	private defaultBody: string;
	private plan: DataPlan = {};
	private queue: { plan: DataPlan; remaining: number }[] = [];
	private inFlight = 0;
	private maxInFlightSeen = 0;

	private constructor(html: string, instance: Server, port: number, body: string) {
		this.html = html;
		this.instance = instance;
		this.origin = `http://127.0.0.1:${port}`;
		this.defaultBody = body;
	}

	/** Sobe o fixture em porta efêmera (127.0.0.1) servindo o HTML real do header. */
	static async start(body = "{}"): Promise<DashboardServer> {
		const html = extractDashboardHtml();
		const holder: { server?: DashboardServer } = {};
		const instance = createServer((request, response) => {
			holder.server?.handle(request, response).catch(() => {
				if (!response.destroyed) {
					response.statusCode = 500;
					response.end("error");
				}
			});
		});
		const port = await new Promise<number>((resolve, reject) => {
			instance.once("error", reject);
			instance.listen(0, "127.0.0.1", () => {
				instance.off("error", reject);
				resolve((instance.address() as AddressInfo).port);
			});
		});
		const server = new DashboardServer(html, instance, port, body);
		holder.server = server;
		return server;
	}

	stop(): void {
		this.instance.closeAllConnections();
		void this.instance.close();
	}

	/** Corpo JSON padrão devolvido em `GET /api/data`. */
	setData(body: string): void {
		this.defaultBody = body;
	}

	/** Plano persistente (até ser trocado). */
	setPlan(plan: DataPlan): void {
		this.plan = plan;
	}

	/** Plano aplicado apenas à próxima requisição de dados. */
	queueOnce(plan: DataPlan): void {
		this.queue.push({ plan, remaining: 1 });
	}

	/** Plano aplicado às próximas `count` requisições de dados. */
	queueFor(count: number, plan: DataPlan): void {
		this.queue.push({ plan, remaining: count });
	}

	/** Volta ao estado saudável e zera contadores de concorrência. */
	reset(body: string): void {
		this.plan = {};
		this.queue = [];
		this.defaultBody = body;
		this.requests.length = 0;
		this.paths.length = 0;
		this.maxInFlightSeen = 0;
	}

	get dataRequests(): RequestRecord[] {
		return this.requests.filter((record) => record.path === "/api/data");
	}

	get maxInFlight(): number {
		return this.maxInFlightSeen;
	}

	private nextPlan(): DataPlan {
		const head = this.queue[0];
		if (!head) {
			return this.plan;
		}
		head.remaining -= 1;
		if (head.remaining <= 0) {
			this.queue.shift();
		}
		return { ...this.plan, ...head.plan };
	}

	private async handle(request: IncomingMessage, response: ServerResponse): Promise<void> {
		const url = new URL(request.url ?? "/", "http://127.0.0.1");
		const path = url.pathname;
		this.paths.push(path);
		if (path === "/") {
			response.writeHead(200, { "content-type": "text/html; charset=utf-8" });
			response.end(this.html);
			return;
		}
		if (path !== "/api/data") {
			response.writeHead(404, { "content-type": "text/plain; charset=utf-8" });
			response.end("Not found");
			return;
		}
		const plan = this.nextPlan();
		const startedAt = Date.now();
		this.inFlight += 1;
		this.maxInFlightSeen = Math.max(this.maxInFlightSeen, this.inFlight);
		const concurrentAtStart = this.inFlight;
		try {
			if (plan.delayMs) {
				await new Promise<void>((resolve) => {
					setTimeout(resolve, plan.delayMs);
				});
			}
			if (response.destroyed) {
				return;
			}
			const status = plan.status ?? 200;
			const body = plan.body ?? this.defaultBody;
			this.requests.push({
				path,
				method: request.method ?? "GET",
				status,
				startedAt,
				endedAt: Date.now(),
				concurrentAtStart,
			});
			response.writeHead(status, { "content-type": plan.contentType ?? DEFAULT_CONTENT_TYPE });
			response.end(body);
		} finally {
			this.inFlight -= 1;
		}
	}
}
