#pragma once
// Callback-free stable ordering using budget-visible Value-owned indices.
#include "rt/collection.h"
namespace nift::rt { bool collection_plain_sort(Value*,const natives::Context&,Fault&); }
