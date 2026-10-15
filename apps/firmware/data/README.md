# data/ — frontend externo do ESP32 (LittleFS)

`/index.html` (mais `index.html.gz`) é **gerado** por `apps/web`
via `bun run web:stage` (dentro de `apps/web`), que copia o bundle para cá antes do upload
(`pio run -t uploadfs`). O comando único `bun run web:deploy` (na raiz) faz build + stage +
build do firmware + `uploadfs` + `upload`.

- Não versionar o bundle: só este README e o `.gitkeep` placeholder.
- Sem `/index.html`, `GET /` usa o `dashboard.h` embarcado (fallback offline).
- `GET /api/data` e o WebSocket da porta 81 não dependem destes arquivos.
