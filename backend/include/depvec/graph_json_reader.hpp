#ifndef DEPVEC_GRAPH_JSON_READER_HPP
#define DEPVEC_GRAPH_JSON_READER_HPP

#include "depvec/dfg.hpp"

#include <string>
#include <string_view>

namespace depvec {

DFG readGraphJson(std::string_view jsonText);
DFG readGraphFile(const std::string &path);

} // namespace depvec

#endif
