#ifndef DEPVEC_VALUE_TYPES_HPP
#define DEPVEC_VALUE_TYPES_HPP

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace depvec {

// Bit widths are ignored, except that i1 values are booleans.
enum class ScalarType { Integer, Boolean };

// The types of the local values an assertion may name, keyed by name without
// %. Values not listed are integers. An aggregate, such as the {T, i1} result
// of cmpxchg, is named only through its scalar members, as %r[0].
struct ValueTypes {
  std::unordered_set<std::string> booleans;
  std::unordered_map<std::string, std::vector<ScalarType>> aggregates;
};

} // namespace depvec

#endif
