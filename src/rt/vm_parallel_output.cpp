#include "rt/vm_parallel_output.h"
#include "rt/parallel_task.h"

#include <algorithm>
#include <iostream>
namespace nift::rt {
void Vm::drain_parallel_output(std::string& output) {
    if (options_.parallel_output && !options_.current_task) options_.parallel_output->drain(output);
}
void Vm::flush_if_needed(std::string& output) {
    if (options_.parallel_output && options_.current_task) options_.parallel_output->publish(output);
    if (output.size() > options_.flush_threshold && options_.flush) options_.flush(output, options_.flush_context);
}
// Output-order repair (D-072 #1): cat's bytes join the program output exactly where a print would, so the flush
// threshold, the parallel-output publication and the end-of-run write apply to them unchanged.
void Vm::native_emit(void* self, std::string_view bytes) {
    Vm& vm = *static_cast<Vm*>(self);
    if (vm.coro_output_ == nullptr) return;
    std::string& output = *vm.coro_output_;
    vm.drain_parallel_output(output);
    output.append(bytes.data(), bytes.size());  // exact bytes, NULs included
    vm.lines_ += static_cast<std::uint64_t>(std::count(bytes.begin(), bytes.end(), '\n'));
    vm.flush_if_needed(output);
}
// D-073 #3: before an operation on the process's own stdout or stderr, every earlier output is on the descriptor, as
// in legacy (which flushes each print). A worker never publishes here: it may not open a std-stream handle.
void Vm::native_publish(void* self) {
    Vm& vm = *static_cast<Vm*>(self);
    if (vm.coro_output_ == nullptr || vm.options_.current_task) return;
    vm.drain_parallel_output(*vm.coro_output_);
    if (vm.options_.flush) vm.options_.flush(*vm.coro_output_, vm.options_.flush_context);
    std::cout.flush();
}
}
