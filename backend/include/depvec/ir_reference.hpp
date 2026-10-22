#ifndef DEPVEC_IR_REFERENCE_HPP
#define DEPVEC_IR_REFERENCE_HPP

#include <string>
#include <variant>

namespace depvec {

// Shared references to LLVM values and the pointer locations currently
// supported by the graph and assertion representations. Names omit % and @.
struct ValueRef {
  std::string name;
};

struct AllocaPointer {
  std::string allocaSite;
};

struct GlobalPointer {
  std::string globalName;
};

using Pointer = std::variant<AllocaPointer, GlobalPointer>;

} // namespace depvec

#endif
