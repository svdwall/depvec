#include "depvec.h"
#include <stdatomic.h>

_Atomic int shared_value;
_Atomic int ready;

DEPVEC_ANALYZE void message_passing_writer(void) {
    atomic_store_explicit(&shared_value, 42, memory_order_relaxed);
    atomic_store_explicit(&ready, 1, memory_order_release);
}

DEPVEC_ANALYZE int message_passing_reader(void) {
    while (atomic_load_explicit(&ready, memory_order_acquire) == 0) {
    }

    return atomic_load_explicit(&shared_value, memory_order_relaxed);
}
