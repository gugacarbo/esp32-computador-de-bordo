// obd.cpp — implementação da coleta OBD (SPEC-0001, ADR-0003, ADR-0004).
//
// SCAFFOLD TEMPORÁRIO (fase RED): a superfície compila e a máquina de sessão ainda não tem
// comportamento. Os testes em apps/firmware/test/test_obd devem falhar antes do GREEN.

#include "obd.h"

namespace onix {

ObdSession::ObdSession(const ObdRuntime& runtime, SnapshotPublisher& publisher)
    : runtime_(runtime), publisher_(&publisher) {}

uint64_t ObdSession::nowMs() const {
  return runtime_.clock != nullptr ? runtime_.clock(runtime_.clockContext) : 0;
}

uint32_t ObdSession::step() { return ONIX_OBD_IDLE_WAIT_MS; }

}  // namespace onix
