#pragma once

#include "theta_filter/core.hpp"

#include <string>
#include <vector>

namespace theta_filter {

// Encodes EventBatch using the wire format declared in proto/theta_events.proto.
// This deliberately has no dependency on the protobuf runtime so it is safe to
// use from the statically deployed collector.
std::string SerializeEventBatchProtobuf(const std::vector<ForwardedEvent>& events);

}  // namespace theta_filter
