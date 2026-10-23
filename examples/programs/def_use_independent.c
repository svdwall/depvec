#include "depvec.h"

volatile int independent_sink;

DEPVEC_ANALYZE int def_use_independent(int input, int unrelated_input) {
    int first = input + 1;
    int unrelated = unrelated_input + 9;
    independent_sink = unrelated;
    int result = first * 2;
    return result;
}
