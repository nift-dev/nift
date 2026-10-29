#include "rt/vm_parallel_output.h"
#include "rt/parallel_task.h"
namespace nift::rt {
void Vm::drain_parallel_output(std::string& output) {
    if (options_.parallel_output && !options_.current_task) options_.parallel_output->drain(output);
}
void Vm::flush_if_needed(std::string& output) {
    if (options_.parallel_output && options_.current_task) options_.parallel_output->publish(output);
    if (output.size() > options_.flush_threshold && options_.flush) options_.flush(output, options_.flush_context);
}
}
