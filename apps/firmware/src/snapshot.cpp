#include "snapshot.h"
namespace onix {
SnapshotStore::SnapshotStore() {
#if ONIX_HAS_FREERTOS_MUTEX
 mutex_ = xSemaphoreCreateMutex();
 configASSERT(mutex_);
#endif
}
SnapshotStore::~SnapshotStore() {
#if ONIX_HAS_FREERTOS_MUTEX
 vSemaphoreDelete(mutex_);
#endif
}
void SnapshotStore::publish(const Snapshot& snapshot) {
#if ONIX_HAS_FREERTOS_MUTEX
 xSemaphoreTake(mutex_, portMAX_DELAY);
 snapshot_ = snapshot;
 xSemaphoreGive(mutex_);
#else
 std::lock_guard<std::mutex> lock(mutex_);
 snapshot_ = snapshot;
#endif
}
Snapshot SnapshotStore::copy() const {
#if ONIX_HAS_FREERTOS_MUTEX
 xSemaphoreTake(mutex_, portMAX_DELAY);
 auto result = snapshot_;
 xSemaphoreGive(mutex_);
 return result;
#else
 std::lock_guard<std::mutex> lock(mutex_);
 return snapshot_;
#endif
}
}
