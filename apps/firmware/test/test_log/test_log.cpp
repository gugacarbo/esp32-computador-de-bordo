// test_log.cpp — testes Unity do buffer de logs e da serialização JSON (ADR-0011).
// Lógica pura (log.h): nenhum include Arduino, roda no env native.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "log.h"
#include "unity.h"

namespace {

using onix::globalLogBuffer;
using onix::LogBuffer;
using onix::LogEntry;
using onix::LogLevel;
using onix::ONIX_LOG_CAPACITY;
using onix::ONIX_LOG_MESSAGE_MAX;
using onix::ONIX_LOG_TAG_MAX;

char g_lastLine[ONIX_LOG_MESSAGE_MAX + 32] = {};
int g_sinkCalls = 0;

void captureSink(const char* line) {
  ++g_sinkCalls;
  std::snprintf(g_lastLine, sizeof g_lastLine, "%s", line);
}

LogEntry makeEntry(uint64_t uptimeMs, LogLevel level, const std::string& tag,
                   const std::string& message) {
  LogEntry entry;
  entry.uptimeMs = uptimeMs;
  entry.level = level;
  entry.tag = tag;
  entry.message = message;
  return entry;
}

void test_nomes_dos_niveis() {
  TEST_ASSERT_EQUAL_STRING("error", onix::logLevelName(LogLevel::Error));
  TEST_ASSERT_EQUAL_STRING("warn", onix::logLevelName(LogLevel::Warn));
  TEST_ASSERT_EQUAL_STRING("info", onix::logLevelName(LogLevel::Info));
  TEST_ASSERT_EQUAL_STRING("debug", onix::logLevelName(LogLevel::Debug));
}

void test_append_e_snapshot_em_ordem() {
  LogBuffer buffer;
  buffer.append(10, LogLevel::Info, "obd", "primeira");
  buffer.append(20, LogLevel::Warn, "api", "segunda");
  buffer.append(30, LogLevel::Error, "ws", "terceira");

  const std::vector<LogEntry> entries = buffer.snapshot();
  TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(entries.size()));
  TEST_ASSERT_EQUAL_UINT64(10, entries[0].uptimeMs);
  TEST_ASSERT_TRUE(entries[0].level == LogLevel::Info);
  TEST_ASSERT_EQUAL_STRING("obd", entries[0].tag.c_str());
  TEST_ASSERT_EQUAL_STRING("primeira", entries[0].message.c_str());
  TEST_ASSERT_EQUAL_UINT64(30, entries[2].uptimeMs);
  TEST_ASSERT_TRUE(entries[2].level == LogLevel::Error);
  TEST_ASSERT_EQUAL_STRING("terceira", entries[2].message.c_str());
  TEST_ASSERT_EQUAL_UINT32(0, buffer.droppedCount());
}

void test_wraparound_descarta_os_mais_antigos() {
  LogBuffer buffer;
  const uint32_t total = static_cast<uint32_t>(ONIX_LOG_CAPACITY) + 5;
  for (uint32_t i = 0; i < total; ++i) {
    char message[32];
    std::snprintf(message, sizeof message, "m%u", static_cast<unsigned>(i));
    buffer.append(i, LogLevel::Info, "obd", message);
  }

  const std::vector<LogEntry> entries = buffer.snapshot();
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(ONIX_LOG_CAPACITY),
                           static_cast<uint32_t>(entries.size()));
  TEST_ASSERT_EQUAL_UINT64(5, entries.front().uptimeMs);  // #0..#4 foram sobrescritas
  TEST_ASSERT_EQUAL_STRING("m5", entries.front().message.c_str());
  TEST_ASSERT_EQUAL_UINT64(total - 1, entries.back().uptimeMs);
  TEST_ASSERT_EQUAL_STRING("m68", entries.back().message.c_str());
  TEST_ASSERT_EQUAL_UINT32(5, buffer.droppedCount());
}

void test_mensagem_longa_e_truncada() {
  LogBuffer buffer;
  const std::string longMessage(200, 'a');
  buffer.append(1, LogLevel::Info, "obd", longMessage.c_str());

  const std::vector<LogEntry> entries = buffer.snapshot();
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(ONIX_LOG_MESSAGE_MAX - 1),
                           static_cast<uint32_t>(entries[0].message.size()));
  TEST_ASSERT_EQUAL_STRING(std::string(ONIX_LOG_MESSAGE_MAX - 1, 'a').c_str(),
                           entries[0].message.c_str());
}

void test_tag_longa_e_truncada() {
  LogBuffer buffer;
  const std::string longTag(20, 'b');
  buffer.append(1, LogLevel::Info, longTag.c_str(), "ok");

  const std::vector<LogEntry> entries = buffer.snapshot();
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(ONIX_LOG_TAG_MAX - 1),
                           static_cast<uint32_t>(entries[0].tag.size()));
  TEST_ASSERT_EQUAL_STRING(std::string(ONIX_LOG_TAG_MAX - 1, 'b').c_str(),
                           entries[0].tag.c_str());
}

void test_tag_e_mensagem_nulas_viram_vazio() {
  LogBuffer buffer;
  buffer.append(7, LogLevel::Debug, nullptr, nullptr);

  const std::vector<LogEntry> entries = buffer.snapshot();
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(entries.size()));
  TEST_ASSERT_EQUAL_STRING("", entries[0].tag.c_str());
  TEST_ASSERT_EQUAL_STRING("", entries[0].message.c_str());
}

void test_clear_zera_buffer_e_contador() {
  LogBuffer buffer;
  for (int i = 0; i < 70; ++i) {
    buffer.append(static_cast<uint64_t>(i), LogLevel::Info, "obd", "x");
  }
  TEST_ASSERT_EQUAL_UINT32(6, buffer.droppedCount());
  buffer.clear();
  TEST_ASSERT_EQUAL_UINT32(0, buffer.droppedCount());
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(buffer.snapshot().size()));
}

void test_json_vazio() {
  const std::string json = onix::serializeLogsJson({}, 0, 42);
  TEST_ASSERT_EQUAL_STRING(R"({"uptimeMs":42,"dropped":0,"entries":[]})", json.c_str());
}

void test_json_uma_entrada() {
  const std::vector<LogEntry> entries = {makeEntry(100, LogLevel::Info, "obd", "ok")};
  const std::string json = onix::serializeLogsJson(entries, 2, 42);
  TEST_ASSERT_EQUAL_STRING(
      R"({"uptimeMs":42,"dropped":2,"entries":[{"uptimeMs":100,"level":"info","tag":"obd","message":"ok"}]})",
      json.c_str());
}

void test_json_escapa_aspas_barra_e_controles() {
  const std::string message = std::string("a\"b\\c\nd\te") + '\x01';
  const std::vector<LogEntry> entries = {makeEntry(1, LogLevel::Warn, "t\"g", message)};
  const std::string json = onix::serializeLogsJson(entries, 0, 1);
  TEST_ASSERT_EQUAL_STRING(
      R"({"uptimeMs":1,"dropped":0,"entries":[{"uptimeMs":1,"level":"warn","tag":"t\"g","message":"a\"b\\c\nd\te\u0001"}]})",
      json.c_str());
}

void test_json_mantem_bytes_utf8() {
  const std::vector<LogEntry> entries = {makeEntry(1, LogLevel::Info, "api", "ºC ok")};
  const std::string json = onix::serializeLogsJson(entries, 0, 1);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"message\":\"\xc2\xba" "C ok\""));
}

void test_logf_filtra_debug_e_grava_info() {
  globalLogBuffer().clear();
  onix::setLogSink(nullptr);

  ONIX_LOGD("obd", "debug %d", 1);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(globalLogBuffer().snapshot().size()));

  ONIX_LOGI("obd", "info %d", 2);
  const std::vector<LogEntry> entries = globalLogBuffer().snapshot();
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(entries.size()));
  TEST_ASSERT_TRUE(entries[0].level == LogLevel::Info);
  TEST_ASSERT_EQUAL_STRING("obd", entries[0].tag.c_str());
  TEST_ASSERT_EQUAL_STRING("info 2", entries[0].message.c_str());
}

void test_logf_nao_envia_debug_ao_sink() {
  globalLogBuffer().clear();
  onix::setLogSink(captureSink);
  g_sinkCalls = 0;

  ONIX_LOGD("obd", "oculto");
  TEST_ASSERT_EQUAL_INT(0, g_sinkCalls);

  onix::setLogSink(nullptr);
}

void test_logf_envia_linha_formatada_ao_sink() {
  globalLogBuffer().clear();
  onix::setLogSink(captureSink);
  g_sinkCalls = 0;
  g_lastLine[0] = '\0';

  ONIX_LOGW("bt", "x=%d", 3);
  TEST_ASSERT_EQUAL_INT(1, g_sinkCalls);
  TEST_ASSERT_EQUAL_STRING("[warn][bt] x=3", g_lastLine);

  const std::vector<LogEntry> entries = globalLogBuffer().snapshot();
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(entries.size()));
  TEST_ASSERT_EQUAL_STRING("x=3", entries[0].message.c_str());

  onix::setLogSink(nullptr);
}

}  // namespace

void setUp() {
  onix::globalLogBuffer().clear();
  onix::setLogSink(nullptr);
  g_sinkCalls = 0;
}

void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nomes_dos_niveis);
  RUN_TEST(test_append_e_snapshot_em_ordem);
  RUN_TEST(test_wraparound_descarta_os_mais_antigos);
  RUN_TEST(test_mensagem_longa_e_truncada);
  RUN_TEST(test_tag_longa_e_truncada);
  RUN_TEST(test_tag_e_mensagem_nulas_viram_vazio);
  RUN_TEST(test_clear_zera_buffer_e_contador);
  RUN_TEST(test_json_vazio);
  RUN_TEST(test_json_uma_entrada);
  RUN_TEST(test_json_escapa_aspas_barra_e_controles);
  RUN_TEST(test_json_mantem_bytes_utf8);
  RUN_TEST(test_logf_filtra_debug_e_grava_info);
  RUN_TEST(test_logf_nao_envia_debug_ao_sink);
  RUN_TEST(test_logf_envia_linha_formatada_ao_sink);
  return UNITY_END();
}
