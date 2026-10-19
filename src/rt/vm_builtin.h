#pragma once
// The VM's call into the CP14 built-in natives (rt/natives): the
// CallNative path for ids from kFirstBuiltin on, defined as Vm::builtin
// (rt/vm.h) in vm_builtin.cpp, off the dispatch hot path.
#include "rt/vm.h"
