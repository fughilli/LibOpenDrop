#ifndef UTIL_GRAPH_TYPES_UNITARY_H_
#define UTIL_GRAPH_TYPES_UNITARY_H_

#include "util/graph/types/opaque_storable.h"
#include "util/graph/types/types.h"
#include "util/logging/logging.h"

namespace opendrop {

struct Unitary : public OpaqueStorable<Monotonic> {
  constexpr static Type kType = Type::kUnitary;

  float value;

  Unitary() : value(0) {}
  Unitary(float value) : value(value) {
    if (value < 0 || value > 1)
      LOG(ERROR) << "Unitary: value must be between 0 and 1, inclusive.";
  }
  operator float() const { return value; }
};

}  // namespace opendrop

#endif  // UTIL_GRAPH_TYPES_UNITARY_H_
