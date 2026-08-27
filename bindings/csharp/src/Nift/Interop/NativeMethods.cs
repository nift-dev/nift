using System.Runtime.InteropServices;

namespace Nift.Interop;

/// <summary>P/Invoke declarations for the frozen Nift C ABI (nift_* symbols).</summary>
internal static partial class Native
{
    private const CallingConvention Cdecl = CallingConvention.Cdecl;

    [DllImport(Lib, EntryPoint = "nift_abi_version", CallingConvention = Cdecl)]
    internal static extern IntPtr nift_abi_version();

    [DllImport(Lib, EntryPoint = "nift_engine_new", CallingConvention = Cdecl)]
    internal static extern IntPtr nift_engine_new();

    [DllImport(Lib, EntryPoint = "nift_engine_open", CallingConvention = Cdecl)]
    internal static extern IntPtr nift_engine_open(IntPtr root, UIntPtr root_len);

    [DllImport(Lib, EntryPoint = "nift_engine_free", CallingConvention = Cdecl)]
    internal static extern void nift_engine_free(IntPtr engine);

    [DllImport(Lib, EntryPoint = "nift_engine_is_open", CallingConvention = Cdecl)]
    internal static extern int nift_engine_is_open(IntPtr engine);

    [DllImport(Lib, EntryPoint = "nift_engine_open_error", CallingConvention = Cdecl)]
    internal static extern int nift_engine_open_error(IntPtr engine, out NiftString out_);

    [DllImport(Lib, EntryPoint = "nift_engine_set_root", CallingConvention = Cdecl)]
    internal static extern int nift_engine_set_root(IntPtr engine, IntPtr root, UIntPtr root_len);

    [DllImport(Lib, EntryPoint = "nift_engine_reload", CallingConvention = Cdecl)]
    internal static extern int nift_engine_reload(IntPtr engine, out NiftString error_out);

    [DllImport(Lib, EntryPoint = "nift_engine_set_string", CallingConvention = Cdecl)]
    internal static extern int nift_engine_set_string(IntPtr engine, IntPtr name, UIntPtr name_len, IntPtr value, UIntPtr value_len);

    [DllImport(Lib, EntryPoint = "nift_engine_set_int", CallingConvention = Cdecl)]
    internal static extern int nift_engine_set_int(IntPtr engine, IntPtr name, UIntPtr name_len, int value);

    [DllImport(Lib, EntryPoint = "nift_engine_set_number", CallingConvention = Cdecl)]
    internal static extern int nift_engine_set_number(IntPtr engine, IntPtr name, UIntPtr name_len, double value);

    [DllImport(Lib, EntryPoint = "nift_engine_set_bool", CallingConvention = Cdecl)]
    internal static extern int nift_engine_set_bool(IntPtr engine, IntPtr name, UIntPtr name_len, int value);

    [DllImport(Lib, EntryPoint = "nift_engine_set_json", CallingConvention = Cdecl)]
    internal static extern int nift_engine_set_json(IntPtr engine, IntPtr name, UIntPtr name_len, IntPtr json, UIntPtr json_len);

    [DllImport(Lib, EntryPoint = "nift_engine_set_loader", CallingConvention = Cdecl)]
    internal static extern int nift_engine_set_loader(IntPtr engine, LoaderCallbackNative callback, IntPtr user_data);

    [DllImport(Lib, EntryPoint = "nift_engine_set_environment_provider", CallingConvention = Cdecl)]
    internal static extern int nift_engine_set_environment_provider(IntPtr engine, EnvironmentCallbackNative callback, IntPtr user_data);

    [DllImport(Lib, EntryPoint = "nift_context_new", CallingConvention = Cdecl)]
    internal static extern IntPtr nift_context_new();

    [DllImport(Lib, EntryPoint = "nift_context_free", CallingConvention = Cdecl)]
    internal static extern void nift_context_free(IntPtr context);

    [DllImport(Lib, EntryPoint = "nift_context_set_page_name", CallingConvention = Cdecl)]
    internal static extern int nift_context_set_page_name(IntPtr context, IntPtr name, UIntPtr name_len);

    [DllImport(Lib, EntryPoint = "nift_context_set_current_output", CallingConvention = Cdecl)]
    internal static extern int nift_context_set_current_output(IntPtr context, IntPtr path, UIntPtr path_len);

    [DllImport(Lib, EntryPoint = "nift_context_set_title", CallingConvention = Cdecl)]
    internal static extern int nift_context_set_title(IntPtr context, IntPtr title, UIntPtr title_len);

    [DllImport(Lib, EntryPoint = "nift_context_set_string", CallingConvention = Cdecl)]
    internal static extern int nift_context_set_string(IntPtr context, IntPtr name, UIntPtr name_len, IntPtr value, UIntPtr value_len);

    [DllImport(Lib, EntryPoint = "nift_context_set_int", CallingConvention = Cdecl)]
    internal static extern int nift_context_set_int(IntPtr context, IntPtr name, UIntPtr name_len, int value);

    [DllImport(Lib, EntryPoint = "nift_context_set_number", CallingConvention = Cdecl)]
    internal static extern int nift_context_set_number(IntPtr context, IntPtr name, UIntPtr name_len, double value);

    [DllImport(Lib, EntryPoint = "nift_context_set_bool", CallingConvention = Cdecl)]
    internal static extern int nift_context_set_bool(IntPtr context, IntPtr name, UIntPtr name_len, int value);

    [DllImport(Lib, EntryPoint = "nift_context_set_json", CallingConvention = Cdecl)]
    internal static extern int nift_context_set_json(IntPtr context, IntPtr name, UIntPtr name_len, IntPtr json, UIntPtr json_len);

    [DllImport(Lib, EntryPoint = "nift_engine_render_page", CallingConvention = Cdecl)]
    internal static extern int nift_engine_render_page(IntPtr engine, IntPtr context, IntPtr page_name, UIntPtr page_name_len, out IntPtr result);

    [DllImport(Lib, EntryPoint = "nift_engine_render", CallingConvention = Cdecl)]
    internal static extern int nift_engine_render(IntPtr engine, in NiftSource page, in NiftSource page_template, IntPtr context, out IntPtr result);

    [DllImport(Lib, EntryPoint = "nift_engine_render_partial", CallingConvention = Cdecl)]
    internal static extern int nift_engine_render_partial(IntPtr engine, in NiftSource partial, IntPtr context, out IntPtr result);

    [DllImport(Lib, EntryPoint = "nift_engine_render_path", CallingConvention = Cdecl)]
    internal static extern int nift_engine_render_path(IntPtr engine, IntPtr context, IntPtr path, UIntPtr path_len, out IntPtr result);

    [DllImport(Lib, EntryPoint = "nift_engine_render_text", CallingConvention = Cdecl)]
    internal static extern int nift_engine_render_text(IntPtr engine, IntPtr context, IntPtr text, UIntPtr text_len, out IntPtr result);

    [DllImport(Lib, EntryPoint = "nift_render_result_free", CallingConvention = Cdecl)]
    internal static extern void nift_render_result_free(IntPtr result);

    [DllImport(Lib, EntryPoint = "nift_render_result_ok", CallingConvention = Cdecl)]
    internal static extern int nift_render_result_ok(IntPtr result);

    [DllImport(Lib, EntryPoint = "nift_render_result_output", CallingConvention = Cdecl)]
    internal static extern int nift_render_result_output(IntPtr result, out NiftString out_);

    [DllImport(Lib, EntryPoint = "nift_render_result_error_message", CallingConvention = Cdecl)]
    internal static extern int nift_render_result_error_message(IntPtr result, out NiftString out_);

    [DllImport(Lib, EntryPoint = "nift_render_result_error_source", CallingConvention = Cdecl)]
    internal static extern int nift_render_result_error_source(IntPtr result, out NiftString out_);

    [DllImport(Lib, EntryPoint = "nift_render_result_error_line", CallingConvention = Cdecl)]
    internal static extern ulong nift_render_result_error_line(IntPtr result);

    [DllImport(Lib, EntryPoint = "nift_render_result_error_column", CallingConvention = Cdecl)]
    internal static extern ulong nift_render_result_error_column(IntPtr result);

    [DllImport(Lib, EntryPoint = "nift_render_result_pagination_count", CallingConvention = Cdecl)]
    internal static extern UIntPtr nift_render_result_pagination_count(IntPtr result);

    [DllImport(Lib, EntryPoint = "nift_render_result_pagination_get", CallingConvention = Cdecl)]
    internal static extern int nift_render_result_pagination_get(IntPtr result, UIntPtr index, out uint page_out, out NiftString output_out);

    [DllImport(Lib, EntryPoint = "nift_render_result_dependency_count", CallingConvention = Cdecl)]
    internal static extern UIntPtr nift_render_result_dependency_count(IntPtr result);

    [DllImport(Lib, EntryPoint = "nift_render_result_dependency_get", CallingConvention = Cdecl)]
    internal static extern int nift_render_result_dependency_get(IntPtr result, UIntPtr index, out NiftString out_);

    [DllImport(Lib, EntryPoint = "nift_render_result_requirement_count", CallingConvention = Cdecl)]
    internal static extern UIntPtr nift_render_result_requirement_count(IntPtr result);

    [DllImport(Lib, EntryPoint = "nift_render_result_requirement_get", CallingConvention = Cdecl)]
    internal static extern int nift_render_result_requirement_get(IntPtr result, UIntPtr index, out NiftString out_);
}
