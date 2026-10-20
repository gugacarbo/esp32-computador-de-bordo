// test_log_file.cpp — testes Unity do histórico persistido de logs (ADR-0018, SPEC-0004).
// Lógica pura (log_file.h): nenhum include Arduino/FS, roda no env native.
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "log_file.h"
#include "unity.h"

namespace {

using onix::LogEntry;
using onix::LogFileStore;
using onix::LogLevel;
using onix::ONIX_LOG_FILE_BATCH;
using onix::ONIX_LOG_FILE_SEGMENT_BYTES;
using onix::ONIX_LOG_FILE_SEGMENTS;

std::string line(uint64_t ts, LogLevel level, const char* tag, const char* message) {
  return onix::formatLogFileLine(ts, level, tag, message);
}

void test_formata_linha_utc() {
  // 2026-10-20T18:36:00Z = 1792521360 s desde 1970-01-01.
  const std::string formatted = line(1792521360000ULL, LogLevel::Warn, "bt", "falha");
  TEST_ASSERT_EQUAL_STRING("2026-10-20T18:36:00Z [warn][bt] falha", formatted.c_str());
}

void test_formata_linha_error() {
  const std::string formatted = line(0, LogLevel::Error, "obd", "sem resposta");
  TEST_ASSERT_EQUAL_STRING("----------------Z [error][obd] sem resposta", formatted.c_str());
}

void test_offer_so_aceita_warn_e_error() {
  LogFileStore store;
  store.offer(LogLevel::Info, "info fica de fora");
  store.offer(LogLevel::Debug, "debug fica de fora");
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(store.pending()));

  store.offer(LogLevel::Warn, "warn entra");
  store.offer(LogLevel::Error, "error entra");
  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(store.pending()));

  const std::vector<std::string> pending = store.takePending();
  TEST_ASSERT_EQUAL_STRING("warn entra", pending[0].c_str());
  TEST_ASSERT_EQUAL_STRING("error entra", pending[1].c_str());
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(store.pending()));
}

void test_lote_cheio_perde_a_mais_nova_e_conta() {
  LogFileStore store;
  for (std::size_t i = 0; i < ONIX_LOG_FILE_BATCH + 3; ++i) {
    char message[16];
    std::snprintf(message, sizeof message, "m%u", static_cast<unsigned>(i));
    store.offer(LogLevel::Warn, message);
  }
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(ONIX_LOG_FILE_BATCH),
                           static_cast<uint32_t>(store.pending()));
  TEST_ASSERT_EQUAL_UINT32(3, store.droppedCount());

  const std::vector<std::string> pending = store.takePending();
  TEST_ASSERT_EQUAL_STRING("m0", pending.front().c_str());
  TEST_ASSERT_EQUAL_STRING("m15", pending.back().c_str());
}

void test_clear_zera_lote_e_contador() {
  LogFileStore store;
  for (int i = 0; i < 20; ++i) {
    store.offer(LogLevel::Error, "x");
  }
  store.clear();
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(store.pending()));
  TEST_ASSERT_EQUAL_UINT32(0, store.droppedCount());
}

void test_parse_linha_valida() {
  const std::vector<std::string> lines = {
      "2026-10-20T18:36:00Z [warn][bt] falha na conexao",
      "2026-10-20T18:36:05Z [error][obd] sem resposta",
  };
  const std::vector<LogEntry> entries = onix::parseLogFileLines(lines);
  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(entries.size()));
  TEST_ASSERT_TRUE(entries[0].level == LogLevel::Warn);
  TEST_ASSERT_EQUAL_STRING("bt", entries[0].tag.c_str());
  TEST_ASSERT_EQUAL_STRING("falha na conexao", entries[0].message.c_str());
  TEST_ASSERT_TRUE(entries[1].level == LogLevel::Error);
  TEST_ASSERT_EQUAL_STRING("sem resposta", entries[1].message.c_str());
}

void test_parse_pula_linhas_corrompidas() {
  const std::vector<std::string> lines = {
      "lixo sem formato",
      "",
      "2026-10-20T18:36:00Z [fatal][bt] nivel invalido",
      "2026-10-20T18:36:00Z [warn][bt] boa",
  };
  const std::vector<LogEntry> entries = onix::parseLogFileLines(lines);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(entries.size()));
  TEST_ASSERT_EQUAL_STRING("boa", entries[0].message.c_str());
}

void test_parse_preserva_mensagem_com_espacos_e_colchetes() {
  const std::vector<std::string> lines = {"2026-10-20T18:36:00Z [info][sys] heap [baixo] 12 B"};
  const std::vector<LogEntry> entries = onix::parseLogFileLines(lines);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(entries.size()));
  TEST_ASSERT_EQUAL_STRING("heap [baixo] 12 B", entries[0].message.c_str());
}

void test_segment_end_nao_corta_linha_pela_metade() {
  std::string text = "linha1\nlinha2\nlinha3";
  // Cabe inteiro: npos (nenhum corte).
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::string::npos),
                           static_cast<uint32_t>(onix::segmentEnd(text, 100)));
  // Corte no meio de 'linha3': devolve o '\n' que fecha 'linha2'.
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(text.rfind('\n')),
                           static_cast<uint32_t>(onix::segmentEnd(text, 14)));
}

void test_segment_end_sem_quebra_devolve_npos() {
  TEST_ASSERT_EQUAL_UINT32(
      static_cast<uint32_t>(std::string::npos),
      static_cast<uint32_t>(onix::segmentEnd("sem quebra alguma", 5)));
}

void test_segments_to_drop_dentro_da_cota() {
  std::array<std::size_t, ONIX_LOG_FILE_SEGMENTS> sizes{};
  sizes[0] = ONIX_LOG_FILE_SEGMENT_BYTES;
  sizes[1] = ONIX_LOG_FILE_SEGMENT_BYTES / 2;
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(onix::segmentsToDrop(sizes)));
}

void test_segments_to_drop_estourando_a_cota() {
  std::array<std::size_t, ONIX_LOG_FILE_SEGMENTS> sizes{};
  for (std::size_t& size : sizes) {
    size = ONIX_LOG_FILE_SEGMENT_BYTES;  // todos cheios: total = cota, ainda cabe
  }
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(onix::segmentsToDrop(sizes)));
  sizes[ONIX_LOG_FILE_SEGMENTS - 1] += 1024;  // estourou: remove o mais antigo
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(onix::segmentsToDrop(sizes)));
}

void test_json_com_historico() {
  const std::vector<LogEntry> ram = {LogEntry{100, LogLevel::Info, "obd", "ok"}};
  std::vector<LogEntry> file(1);
  file[0].level = LogLevel::Warn;
  file[0].tag = "bt";
  file[0].message = "falha";
  const std::string json = onix::serializeLogHistoryJson(ram, 2, file, 1, 512, 42);
  TEST_ASSERT_EQUAL_STRING(
      R"({"uptimeMs":42,"dropped":2,"droppedLack":1,"persisted":512,"entries":[{"uptimeMs":100,"level":"info","tag":"obd","message":"ok"}],"history":[{"level":"warn","tag":"bt","message":"falha"}]})",
      json.c_str());
}

void test_json_historico_vazio() {
  const std::string json = onix::serializeLogHistoryJson({}, 0, {}, 0, 0, 7);
  TEST_ASSERT_EQUAL_STRING(
      R"({"uptimeMs":7,"dropped":0,"droppedLack":0,"persisted":0,"entries":[],"history":[]})",
      json.c_str());
}

void test_log_file_line_grava_no_lote_global() {
  onix::globalLogFileStore().clear();
  onix::logFileLine(1792521360000ULL, LogLevel::Warn, "bt", "global");
  const std::vector<std::string> pending = onix::globalLogFileStore().takePending();
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(pending.size()));
  TEST_ASSERT_EQUAL_STRING("2026-10-20T18:36:00Z [warn][bt] global", pending[0].c_str());
}

void test_log_file_line_ignora_info() {
  onix::globalLogFileStore().clear();
  onix::logFileLine(1, LogLevel::Info, "sys", "metricas");
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(onix::globalLogFileStore().pending()));
}

void test_log_path() {
  TEST_ASSERT_EQUAL_STRING("/logs.0.txt", onix::logFilePath(0).c_str());
  TEST_ASSERT_EQUAL_STRING("/logs.3.txt", onix::logFilePath(3).c_str());
}

}  // namespace

void setUp() { onix::globalLogFileStore().clear(); }

void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_formata_linha_utc);
  RUN_TEST(test_formata_linha_error);
  RUN_TEST(test_offer_so_aceita_warn_e_error);
  RUN_TEST(test_lote_cheio_perde_a_mais_nova_e_conta);
  RUN_TEST(test_clear_zera_lote_e_contador);
  RUN_TEST(test_parse_linha_valida);
  RUN_TEST(test_parse_pula_linhas_corrompidas);
  RUN_TEST(test_parse_preserva_mensagem_com_espacos_e_colchetes);
  RUN_TEST(test_segment_end_nao_corta_linha_pela_metade);
  RUN_TEST(test_segment_end_sem_quebra_devolve_npos);
  RUN_TEST(test_segments_to_drop_dentro_da_cota);
  RUN_TEST(test_segments_to_drop_estourando_a_cota);
  RUN_TEST(test_json_com_historico);
  RUN_TEST(test_json_historico_vazio);
  RUN_TEST(test_log_file_line_grava_no_lote_global);
  RUN_TEST(test_log_file_line_ignora_info);
  RUN_TEST(test_log_path);
  return UNITY_END();
}
