#ifndef DEPVEC_IR_REFERENCE_HPP
#define DEPVEC_IR_REFERENCE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace depvec {

// Shared references to LLVM values and the pointer locations currently
// supported by the graph and assertion representations. Names omit % and @.
// A field names one scalar member of a flat aggregate, such as the {T, i1}
// result of cmpxchg; the aggregate itself is never a value.
struct ValueRef {
  std::string name;
  std::optional<std::uint32_t> field;
};

inline bool sameValue(const ValueRef &left, const ValueRef &right) {
  return left.name == right.name && left.field == right.field;
}

// The value as written in assertions, without %: r or r[0].
inline std::string valueText(const ValueRef &value) {
  if (!value.field)
    return value.name;
  return value.name + "[" + std::to_string(*value.field) + "]";
}

struct AllocaPointer {
  std::string allocaSite;
};

struct GlobalPointer {
  std::string globalName;
};

using Pointer = std::variant<AllocaPointer, GlobalPointer>;

} // namespace depvec

#endif
