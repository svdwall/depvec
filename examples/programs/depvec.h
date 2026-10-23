#ifndef DEPVEC_EXAMPLE_H
#define DEPVEC_EXAMPLE_H

#if defined(__clang__)
#define DEPVEC_ANALYZE __attribute__((annotate("depvec")))
#else
#define DEPVEC_ANALYZE
#endif

#endif
