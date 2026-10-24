#include "rt/vm_handles.h"

#include "rt/handles/file_resource.h"
#include "rt/natives/host_file_handles.h"
#include "rt/natives/host_stream_handles.h"

namespace nift::rt {

void Vm::open_handle_runtime() {
    handles::KindSpec file_spec;
    file_spec.name = "file";
    file_spec.cleanup = natives::cleanup_file;
    handle_table_.register_typed_kind<handles::FileResource>(HandleKind::File, file_spec);
    handles::KindSpec stream_spec;
    stream_spec.name = "stream";
    stream_spec.cleanup = natives::cleanup_stream;
    handle_table_.register_typed_kind<handles::StreamResource>(HandleKind::Stream, stream_spec);
    handle_context_open_ =
        handle_table_.open_context(handles::ContextKind::Script, handles::kDetached, handle_context_, "script") ==
        HandleStatus::Ok;
}

void Vm::close_handle_runtime(VmResult& result) {
    if (!handle_context_open_) return;
    const handles::CloseReport close = handle_table_.close_context(handle_context_);
    handle_context_open_ = false;
    if (result.reason == StopReason::None && close.cleanup_failed()) {
        result.reason = StopReason::Error;
        result.message = close.failures.front().message;
        result.code = close.failures.front().code;
    }
}

} // namespace nift::rt
