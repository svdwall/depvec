#include "depvec.h"
#include <stdatomic.h>

volatile _Atomic int x;
volatile _Atomic int y;
int z;

DEPVEC_ANALYZE void example(void) {
    atomic_store_explicit(&x, 5, memory_order_relaxed);
    z = atomic_load_explicit(&y, memory_order_relaxed);
}
