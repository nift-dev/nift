#pragma once
// Cold structural storage-location facts, without resolving captured values.
#include "rt/session_callable.h"
namespace nift::rt {
struct SessionCompileContext;
void clear_session_locations(SessionCompileContext&);
void seed_session_locations(SessionCompileContext&, const std::vector<Value>&);
}
