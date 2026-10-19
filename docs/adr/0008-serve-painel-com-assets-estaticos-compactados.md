---
status: superseded # proposed | accepted | deprecated | superseded
date: 2026-10-19
builds-on: [ADR-0007] # ADRs relacionados: ADR-NNNN ou repo:ADR-NNNN
superseded-by: ADR-0010 # escalar; preenchido no ADR antigo no commit que aceita o novo
deciders: ["project maintainer"]
---

> ⚠️ VERDADE ATUAL: assets separados com hash, gzip e políticas de cache permanecem; as cópias sem compressão e a negociação de `Accept-Encoding` foram removidas na ADR-0010.

# Serve painel com assets estáticos compactados

## Contexto e problema

ADR-0007 adotou o bundle Vite singlefile no LittleFS. O build atual produz um `index.html` de
273.208 bytes e uma variante gzip de 84.547 bytes, mas o stage copia apenas o HTML sem compressão.
Assim, o ESP32 envia mais bytes do que o necessário e o navegador não pode reutilizar CSS e
JavaScript em cache independente. Separar os assets permite carregá-los em paralelo e reutilizar
nomes com hash; a transferência gzip reduz o volume da primeira carga.

## Direcionadores da decisão

- Continuar servindo o painel pela rede local do ESP32, sem internet nem CDN.
- Reduzir bytes transferidos no celular e permitir cache independente de assets versionados.
- Preservar `GET /api/data`, WebSocket, fallback embarcado e o deploy único do repositório.
- Manter o total dos arquivos staged em até 600 KB.

## Opções consideradas

### Opção 1 — manter o HTML único sem compressão

**Prós:** uma requisição e stage simples.
**Contras:** transfere o HTML sem compressão e invalida todos os recursos juntos.

### Opção 2 — separar os assets sem compressão

**Prós:** CSS e JavaScript podem carregar em paralelo e ter cache independente.
**Contras:** cria requisições adicionais sem reduzir o volume transferido.

### Opção 3 — separar assets com hash e servir gzip

**Prós:** reduz bytes transferidos, habilita cache imutável dos assets com hash e mantém a página
offline.
**Contras:** aumenta o número de arquivos no LittleFS e exige negociar a codificação no servidor.

## Decisão

Gerar o HTML com CSS e JavaScript separados em arquivos versionados, publicar variantes gzip dos
assets e servir a variante comprimida quando o cliente anunciar suporte a gzip. O HTML, menor que
1 KB, deve ser revalidado; assets com hash podem usar cache imutável. O stage copia a saída completa
do Vite e aplica o limite de 600 KB ao total dos arquivos publicados.

## Consequências

- A página continua na mesma origem local; não surgem dependências externas.
- O servidor deve retornar o Content-Type do arquivo original, `Content-Encoding: gzip` quando
  enviar a variante comprimida e `Vary: Accept-Encoding` para evitar cache incorreto.
- O HTML usa `Cache-Control: no-cache`; assets com hash usam `Cache-Control: public, max-age=31536000, immutable`.
- O stage deve preservar a árvore e os sidecars `.gz` dos assets; o servidor usa o arquivo sem
  compressão como fallback.
- Dividir os arquivos por si só não garante uma primeira carga mais rápida; gzip reduz o volume e o
  cache dos assets ajuda nas cargas seguintes.

## Confirmação

```bash
bun run web:deploy --build-only
```

O comando deve produzir/stagear HTML e assets separados dentro do limite. Na placa, uma requisição
de navegador que aceita gzip deve receber os assets com `Content-Encoding: gzip`; `GET /` deve
revalidar o HTML e os assets com hash devem indicar cache imutável.

## Notas

Esta ADR supera ADR-0007 somente quanto ao formato e à entrega dos assets do painel; a escolha de
LittleFS, WebSocket e deploy local permanece.
