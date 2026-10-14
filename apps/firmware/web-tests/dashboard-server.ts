// dashboard-server.ts — fixture Bun usada SOMENTE pelos testes Playwright da SPEC-0003.
// Serve o HTML real extraído de apps/firmware/include/dashboard.h e um `GET /api/data` programável,
// para exercitar polling, erro HTTP, JSON inválido, resposta lenta e recuperação sem hardware.
// Não é servidor de produção nem substitui o WebServer do ESP32.
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
	private readonly instance: ReturnType<typeof Bun.serve>;
	private defaultBody: string;
	private plan: DataPlan = {};
	private queue: { plan: DataPlan; remaining: number }[] = [];
	private inFlight = 0;
	private maxInFlightSeen = 0;

	private constructor(html: string, instance: ReturnType<typeof Bun.serve>, body: string) {
		this.html = html;
		this.instance = instance;
		this.origin = `http://127.0.0.1:${instance.port}`;
		this.defaultBody = body;
	}

	/** Sobe o fixture em porta efêmera servindo o HTML real do header. */
	static start(body = "{}"): DashboardServer {
		const html = extractDashboardHtml();
		const holder: { server?: DashboardServer } = {};
		const instance = Bun.serve({
			port: 0,
			fetch: (request) => holder.server?.handle(request) ?? new Response("starting", { status: 503 }),
		});
		const server = new DashboardServer(html, instance, body);
		holder.server = server;
		return server;
	}

	stop(): void {
		this.instance.stop(true);
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

	private async handle(request: Request): Promise<Response> {
		const url = new URL(request.url);
		const path = url.pathname;
		this.paths.push(path);
		if (path === "/") {
			return new Response(this.html, {
				status: 200,
				headers: { "content-type": "text/html; charset=utf-8" },
			});
		}
		if (path !== "/api/data") {
			return new Response("Not found", { status: 404 });
		}
		const plan = this.nextPlan();
		const startedAt = Date.now();
		this.inFlight += 1;
		this.maxInFlightSeen = Math.max(this.maxInFlightSeen, this.inFlight);
		const concurrentAtStart = this.inFlight;
		try {
			if (plan.delayMs) {
				await Bun.sleep(plan.delayMs);
			}
			const status = plan.status ?? 200;
			const body = plan.body ?? this.defaultBody;
			this.requests.push({
				path,
				method: request.method,
				status,
				startedAt,
				endedAt: Date.now(),
				concurrentAtStart,
			});
			return new Response(body, {
				status,
				headers: { "content-type": plan.contentType ?? DEFAULT_CONTENT_TYPE },
			});
		} finally {
			this.inFlight -= 1;
		}
	}
}
