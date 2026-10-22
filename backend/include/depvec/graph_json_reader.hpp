#ifndef DEPVEC_GRAPH_JSON_READER_HPP
#define DEPVEC_GRAPH_JSON_READER_HPP

#include "depvec/graph.hpp"

#include <string>
#include <string_view>

namespace depvec {

Graph readGraphJson(std::string_view jsonText);
Graph readGraphFile(const std::string &path);

} // namespace depvec

#endif
