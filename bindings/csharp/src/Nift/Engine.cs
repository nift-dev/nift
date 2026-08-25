using System.Runtime.InteropServices;
using System.Text;
using Nift.Interop;

namespace Nift;

/// <summary>
/// A Nift Embed engine: a thin semantic adapter over the frozen C ABI.
/// Deterministic disposal via SafeHandle; mutation (setters, loader/env
/// installation, SetRoot) is not thread-safe with active renders, matching the
/// C ABI contract. Renders themselves are safe to run concurrently.
///
/// Callback delegates are rooted as strong fields for the engine lifetime so
/// native code can never invoke a collected delegate. Callback `out` buffers
/// use the per-thread scratch described in Interop/Callbacks.cs.
/// </summary>
public sealed class Engine : IDisposable
{
    private readonly EngineHandle _handle;
    private GCHandle _userDataHandle;
    private readonly LoaderCallbackNative _loaderBridge;
    private readonly EnvironmentCallbackNative _envBridge;
    private HostLoader? _loader;
    private HostEnvironment? _env;
    private bool _disposed;

    private Engine(IntPtr handle)
    {
        _handle = new EngineHandle(handle);
        _userDataHandle = GCHandle.Alloc(this, GCHandleType.Normal);
        _loaderBridge = OnLoader;
        _envBridge = OnEnvironment;
    }

    /// <summary>Standalone engine (deterministic; never walks the filesystem).</summary>
    public static Engine New()
    {
        IntPtr handle = Native.nift_engine_new();
        if (handle == IntPtr.Zero)
        {
            throw new NiftException("nift_engine_new returned null");
        }
        return new Engine(handle);
    }

    /// <summary>Project-aware engine opening the Nift project at `root`. Check
    /// IsOpen/OpenError after construction; the call itself is non-throwing.</summary>
    public static Engine Open(string root)
    {
        using VarString vars = new(root);
        IntPtr handle = Native.nift_engine_open(vars.Name, vars.NameLen);
        if (handle == IntPtr.Zero)
        {
            throw new NiftException("nift_engine_open returned null");
        }
        return new Engine(handle);
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }
        _disposed = true;
        if (_userDataHandle.IsAllocated)
        {
            _userDataHandle.Free();
        }
        _loader = null;
        _env = null;
        _handle.Dispose();
        GC.SuppressFinalize(this);
    }

    public bool IsOpen() => _handle.IsInvalid == false && Native.nift_engine_is_open(_handle.DangerousGetHandle()) != 0;

    public string OpenError()
    {
        int rc = Native.nift_engine_open_error(_handle.DangerousGetHandle(), out NiftString out_);
        return rc == NativeStatus.Ok ? Utf8.FromNative(out_) : "";
    }

    /// <summary>Base directory for resolving relative path sources and relative @input.</summary>
    public void SetRoot(string root)
    {
        EnsureNotDisposed();
        using VarString vars = new(root);
        int rc = Native.nift_engine_set_root(_handle.DangerousGetHandle(), vars.Name, vars.NameLen);
        if (rc != NativeStatus.Ok)
        {
            throw new NiftException("nift_engine_set_root failed");
        }
    }

    /// <summary>Atomic snapshot replacement (Engine::reload). Safe concurrently with renders.</summary>
    public void Reload()
    {
        EnsureNotDisposed();
        int rc = Native.nift_engine_reload(_handle.DangerousGetHandle(), out NiftString errorOut);
        if (rc != NativeStatus.Ok)
        {
            throw new NiftException(Utf8.FromNative(errorOut) is { Length: > 0 } message ? message : "nift_engine_reload failed");
        }
    }

    public void SetLoader(HostLoader? loader)
    {
        EnsureNotDisposed();
        _loader = loader;
        int rc = Native.nift_engine_set_loader(_handle.DangerousGetHandle(), _loaderBridge, GCHandle.ToIntPtr(_userDataHandle));
        if (rc != NativeStatus.Ok)
        {
            throw new NiftException("nift_engine_set_loader failed");
        }
    }

    public void SetEnvironmentProvider(HostEnvironment? env)
    {
        EnsureNotDisposed();
        _env = env;
        int rc = Native.nift_engine_set_environment_provider(_handle.DangerousGetHandle(), _envBridge, GCHandle.ToIntPtr(_userDataHandle));
        if (rc != NativeStatus.Ok)
        {
            throw new NiftException("nift_engine_set_environment_provider failed");
        }
    }

    public void SetString(string name, string value) => SetStringValue(Native.nift_engine_set_string, name, value);

    public void SetInt(string name, int value) => SetScalar(Native.nift_engine_set_int, name, value);

    public void SetNumber(string name, double value) => SetDouble(Native.nift_engine_set_number, name, value);

    public void SetBool(string name, bool value) => SetScalar(Native.nift_engine_set_bool, name, value ? 1 : 0);

    public void SetJSON(string name, string json) => SetStringValue(Native.nift_engine_set_json, name, json);

    /// <summary>Project-aware tracked-page render with complete pagination.</summary>
    public RenderResult RenderPage(string pageName, Context? ctx = null)
    {
        EnsureNotDisposed();
        using VarString vars = new(pageName);
        IntPtr contextHandle = ctx is null ? IntPtr.Zero : ctx.Handle.DangerousGetHandle();
        int rc = Native.nift_engine_render_page(_handle.DangerousGetHandle(), contextHandle, vars.Name, vars.NameLen, out IntPtr resultPtr);
        return ConsumeResult(rc, resultPtr);
    }

    /// <summary>Full page + template composition (template must contain exactly one @content).</summary>
    public RenderResult Render(RenderSource page, RenderSource template, Context? ctx = null)
    {
        EnsureNotDisposed();
        IntPtr contextHandle = ctx is null ? IntPtr.Zero : ctx.Handle.DangerousGetHandle();
        using var sources = new NativeSources(page, template);
        int rc = Native.nift_engine_render(_handle.DangerousGetHandle(), in sources.Page, in sources.Template, contextHandle, out IntPtr resultPtr);
        return ConsumeResult(rc, resultPtr);
    }

    /// <summary>Convenience: render in-memory page and template text.</summary>
    public RenderResult Render(string page, string template, Context? ctx = null)
        => Render(RenderSource.Text(page), RenderSource.Text(template), ctx);

    /// <summary>Standalone partial/fragment render (a partial containing @content is an error).</summary>
    public RenderResult RenderPartial(RenderSource partial, Context? ctx = null)
    {
        EnsureNotDisposed();
        IntPtr contextHandle = ctx is null ? IntPtr.Zero : ctx.Handle.DangerousGetHandle();
        using var sources = new NativeSources(partial);
        int rc = Native.nift_engine_render_partial(_handle.DangerousGetHandle(), in sources.Partial, contextHandle, out IntPtr resultPtr);
        return ConsumeResult(rc, resultPtr);
    }

    public RenderResult RenderPartial(string partial, Context? ctx = null)
        => RenderPartial(RenderSource.Text(partial), ctx);

    private RenderResult ConsumeResult(int rc, IntPtr resultPtr)
    {
        if (rc != NativeStatus.Ok)
        {
            throw new NiftException("render call failed");
        }
        using var resultHandle = new ResultHandle(resultPtr);
        int ok = Native.nift_render_result_ok(resultHandle.DangerousGetHandle());
        if (ok == 0)
        {
            string? message = null;
            if (Native.nift_render_result_error_message(resultHandle.DangerousGetHandle(), out NiftString messageOut) == NativeStatus.Ok)
            {
                message = Utf8.FromNative(messageOut);
            }
            string? source = null;
            if (Native.nift_render_result_error_source(resultHandle.DangerousGetHandle(), out NiftString sourceOut) == NativeStatus.Ok)
            {
                source = Utf8.FromNative(sourceOut);
            }
            ulong line = Native.nift_render_result_error_line(resultHandle.DangerousGetHandle());
            ulong column = Native.nift_render_result_error_column(resultHandle.DangerousGetHandle());
            return new RenderResult
            {
                Ok = false,
                Output = "",
                ErrorMessage = message,
                ErrorSource = source,
                ErrorLine = line,
                ErrorColumn = column,
            };
        }

        string output = Native.nift_render_result_output(resultHandle.DangerousGetHandle(), out NiftString outputOut) == NativeStatus.Ok
            ? Utf8.FromNative(outputOut)
            : "";

        var pagination = new List<PaginatedPage>();
        UIntPtr pageCount = Native.nift_render_result_pagination_count(resultHandle.DangerousGetHandle());
        for (UIntPtr i = UIntPtr.Zero; i.ToUInt64() < pageCount.ToUInt64(); i = (UIntPtr)(i.ToUInt64() + 1))
        {
            if (Native.nift_render_result_pagination_get(resultHandle.DangerousGetHandle(), i, out uint pageNo, out NiftString pageOutput) == NativeStatus.Ok)
            {
                pagination.Add(new PaginatedPage(pageNo, Utf8.FromNative(pageOutput)));
            }
        }

        var dependencies = new List<string>();
        UIntPtr depCount = Native.nift_render_result_dependency_count(resultHandle.DangerousGetHandle());
        for (UIntPtr i = UIntPtr.Zero; i.ToUInt64() < depCount.ToUInt64(); i = (UIntPtr)(i.ToUInt64() + 1))
        {
            if (Native.nift_render_result_dependency_get(resultHandle.DangerousGetHandle(), i, out NiftString depOut) == NativeStatus.Ok)
            {
                dependencies.Add(Utf8.FromNative(depOut));
            }
        }

        var requirements = new List<string>();
        UIntPtr reqCount = Native.nift_render_result_requirement_count(resultHandle.DangerousGetHandle());
        for (UIntPtr i = UIntPtr.Zero; i.ToUInt64() < reqCount.ToUInt64(); i = (UIntPtr)(i.ToUInt64() + 1))
        {
            if (Native.nift_render_result_requirement_get(resultHandle.DangerousGetHandle(), i, out NiftString reqOut) == NativeStatus.Ok)
            {
                requirements.Add(Utf8.FromNative(reqOut));
            }
        }

        return new RenderResult
        {
            Ok = true,
            Output = output,
            Pagination = pagination,
            Dependencies = dependencies,
            Requirements = requirements,
        };
    }

    private int OnLoader(IntPtr userData, IntPtr path, UIntPtr pathLen, ref NiftString out_)
    {
        string pathString = Utf8.FromNative(path, pathLen);
        HostResult result = _loader?.Invoke(pathString) ?? HostResult.NotFound();
        return FillHostResult(result, ref out_);
    }

    private int OnEnvironment(IntPtr userData, IntPtr name, UIntPtr nameLen, ref NiftString out_)
    {
        string nameString = Utf8.FromNative(name, nameLen);
        HostResult result = _env?.Invoke(nameString) ?? HostResult.NotFound();
        return FillHostResult(result, ref out_);
    }

    private static int FillHostResult(HostResult result, ref NiftString out_)
    {
        switch (result.Status)
        {
            case HostStatus.Found:
                return Fill(Encoding.UTF8.GetBytes(result.Value), ref out_, NativeStatus.Ok);
            case HostStatus.NotFound:
                out_ = default;
                return NativeStatus.NotFound;
            default:
                return Fill(Encoding.UTF8.GetBytes(result.Diagnostic ?? ""), ref out_, NativeStatus.Callback);
        }
    }

    private static int Fill(byte[] bytes, ref NiftString out_, int status)
    {
        if (bytes.Length == 0)
        {
            out_ = default;
            return status;
        }
        IntPtr scratch = CallbackScratch.Prepare(bytes.Length);
        Marshal.Copy(bytes, 0, scratch, bytes.Length);
        out_ = new NiftString { Data = scratch, Length = (UIntPtr)bytes.Length };
        return status;
    }

    private void EnsureNotDisposed()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
    }

    private void SetStringValue(EngineStringSetter setter, string name, string value)
    {
        EnsureNotDisposed();
        using VarString vars = new(name, value);
        int rc = setter(_handle.DangerousGetHandle(), vars.Name, vars.NameLen, vars.Value, vars.ValueLen);
        if (rc != NativeStatus.Ok)
        {
            throw new NiftException($"invalid binding name: {name}");
        }
    }

    private void SetScalar(EngineScalarSetter setter, string name, int value)
    {
        EnsureNotDisposed();
        using VarString vars = new(name);
        int rc = setter(_handle.DangerousGetHandle(), vars.Name, vars.NameLen, value);
        if (rc != NativeStatus.Ok)
        {
            throw new NiftException($"invalid binding name: {name}");
        }
    }

    private void SetDouble(EngineDoubleSetter setter, string name, double value)
    {
        EnsureNotDisposed();
        using VarString vars = new(name);
        int rc = setter(_handle.DangerousGetHandle(), vars.Name, vars.NameLen, value);
        if (rc != NativeStatus.Ok)
        {
            throw new NiftException($"invalid binding name: {name}");
        }
    }

    private delegate int EngineStringSetter(IntPtr engine, IntPtr name, UIntPtr name_len, IntPtr value, UIntPtr value_len);
    private delegate int EngineScalarSetter(IntPtr engine, IntPtr name, UIntPtr name_len, int value);
    private delegate int EngineDoubleSetter(IntPtr engine, IntPtr name, UIntPtr name_len, double value);

    private sealed class EngineHandle : SafeHandle
    {
        public EngineHandle(IntPtr handle) : base(IntPtr.Zero, ownsHandle: true) => SetHandle(handle);

        public override bool IsInvalid => handle == IntPtr.Zero;

        protected override bool ReleaseHandle()
        {
            Native.nift_engine_free(handle);
            return true;
        }
    }

    private sealed class ResultHandle : SafeHandle
    {
        public ResultHandle(IntPtr handle) : base(IntPtr.Zero, ownsHandle: true) => SetHandle(handle);

        public override bool IsInvalid => handle == IntPtr.Zero;

        protected override bool ReleaseHandle()
        {
            Native.nift_render_result_free(handle);
            return true;
        }
    }
}
