#pragma once
// Immutable callable facts; never copies or pins executable Values or Programs.
#include "rt/session_definition.h"
#include "rt/infer_types.h"
#include "rt/value.h"
#include <map>
#include <vector>
namespace nift::rt {
struct SessionCallableShape;
struct SessionLocationEdge {
    std::uintptr_t child = 0, parent = 0;
    bool element = false;
    std::string field;
    bool member = false;  // otherwise !element means lexical forwarding
};
struct SessionCaptureLocation {
    std::uintptr_t key = 0;
    std::vector<SessionLocationEdge> edges;
};
SessionCaptureLocation session_capture_location(const Value&);
struct SessionCallable {
    std::uint64_t identity = 0;
    std::shared_ptr<const SessionDefinition> definition;
    std::vector<std::shared_ptr<const SessionCallableShape>> captures;
    std::vector<std::shared_ptr<const infer_detail::Ty>> parameter_types, capture_types;
    std::vector<SessionCaptureLocation> capture_locations;
};
struct SessionCallableShape {
    bool complete = true;
    std::vector<SessionCallable> targets;
    std::map<std::string, std::shared_ptr<const SessionCallableShape>, std::less<>> fields;
    std::shared_ptr<const SessionCallableShape> element;
};
std::shared_ptr<const SessionCallableShape> merge_session_callable_shapes(
    const std::shared_ptr<const SessionCallableShape>&, const std::shared_ptr<const SessionCallableShape>&);
std::shared_ptr<const SessionCallableShape> session_callable_shape(const Value& value);
std::vector<SessionCallable> session_callable_targets(const std::shared_ptr<const SessionCallableShape>& shape);
} // namespace nift::rt
