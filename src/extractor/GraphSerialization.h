#ifndef DEPVEC_GRAPH_SERIALIZATION_H
#define DEPVEC_GRAPH_SERIALIZATION_H

#include "DependencyGraphAnalysis.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"

#include <vector>

namespace llvm {
class Function;
class Module;
} // namespace llvm

namespace depvec {

llvm::json::Object serializeFunction(
    const llvm::Function &F, const DependencyGraph &Graph);
bool writeGraphJSON(const llvm::Module &M,
                    std::vector<llvm::json::Object> Functions,
                    llvm::StringRef Path);

} // namespace depvec

#endif
