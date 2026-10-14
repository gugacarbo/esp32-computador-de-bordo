// dashboard.h — painel offline do ESP32 (SPEC-0003, ADR-0005): HTML, CSS e JavaScript locais.
// Etapa RED: apenas o literal bruto extraído pelos testes de Playwright em apps/firmware/web-tests.
// A interface completa entra na etapa GREEN.
#pragma once

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <pgmspace.h>
#define ONIX_DASHBOARD_FLASH PROGMEM
#else
#define ONIX_DASHBOARD_FLASH
#endif

namespace onix {

// Todo o documento em um único literal bruto R"HTML(...)HTML" para que os testes extraiam
// exatamente o que o firmware serve (nunca uma cópia independente do HTML).
static const char DASHBOARD_HTML[] ONIX_DASHBOARD_FLASH = R"HTML(<!DOCTYPE html>
<html lang="pt-BR">
	<head>
		<meta charset="utf-8" />
		<title>ONIX</title>
	</head>
	<body>
		<p>ONIX</p>
	</body>
</html>)HTML";

}  // namespace onix

#undef ONIX_DASHBOARD_FLASH
