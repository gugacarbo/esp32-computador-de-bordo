import type { ReactElement } from "react";
import {
	formatSensorValue,
	SENSOR_UNITS,
	type SensorKey,
	type SensorReading,
	type StatusTone,
	sensorStatusInfo,
} from "../lib/envelope";
import { Badge } from "./ui/badge";
import { Card, CardContent, CardHeader, CardTitle } from "./ui/card";

interface SensorCardProps {
	sensor: SensorKey;
	title: string;
	reading: SensorReading | null;
	offline: boolean;
}

export function SensorCard({
	sensor,
	title,
	reading,
	offline,
}: SensorCardProps): ReactElement {
	const unit = SENSOR_UNITS[sensor];
	let value = "—";
	let state = "";
	let tone: StatusTone = "muted";
	let statusAttr = "unknown";
	let note = "";

	if (offline) {
		statusAttr = "offline";
		state = "Sem dados atuais";
		if (reading !== null && typeof reading.value === "number") {
			note = `Leitura anterior: ${formatSensorValue(sensor, reading.value)} ${unit}`;
		}
	} else if (reading !== null) {
		const okReading = reading.valid && reading.status === "ok";
		statusAttr = okReading ? "ok" : reading.status;
		const info = sensorStatusInfo(reading);
		state = info.label;
		tone = info.tone;
		if (typeof reading.value === "number") {
			if (okReading) {
				value = formatSensorValue(sensor, reading.value);
			} else {
				note = `Leitura anterior: ${formatSensorValue(sensor, reading.value)} ${unit}`;
			}
		}
	}

	return (
		<Card id={`card-${sensor}`} data-sensor={sensor} data-status={statusAttr}>
			<CardHeader>
				<CardTitle>{title}</CardTitle>
			</CardHeader>
			<CardContent className="flex flex-col gap-1">
				<p className="m-0 text-3xl leading-none tabular-nums">
					<span id={`value-${sensor}`}>{value}</span>
					<span className="ml-1 text-sm text-slate-400">{unit}</span>
				</p>
				{state !== "" && <Badge tone={tone}>{state}</Badge>}
				{note !== "" && (
					<p className="m-0 min-h-4 text-xs text-slate-400">{note}</p>
				)}
			</CardContent>
		</Card>
	);
}
