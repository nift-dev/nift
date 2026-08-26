using System.Runtime.InteropServices;
using Nift.Interop;

namespace Nift;

/// <summary>
/// Per-render request state (page name, current output, title, bindings).
/// Deterministically disposed; the underlying nift_context is freed on
/// Dispose/finalize.
/// </summary>
public sealed class Context : IDisposable
{
    private SafeHandle? _handle;
    private readonly object _lifecycle = new();
    private int _renderCount;
    private bool _disposed;
    private bool _destroyed;

    internal SafeHandle Handle
    {
        get
        {
            lock (_lifecycle)
            {
                ObjectDisposedException.ThrowIf(_disposed || _destroyed, this);
                return _handle!;
            }
        }
    }

    public Context()
    {
        _handle = new ContextHandle(Native.nift_context_new());
    }

    /// <summary>Logically disposes the context: new use is rejected
    /// immediately, but the native context is freed only after the last
    /// in-flight render using it quiesces. Idempotent.</summary>
    public void Dispose()
    {
        lock (_lifecycle)
        {
            _disposed = true;
            if (_renderCount == 0)
            {
                DestroyNow();
            }
        }
    }

    internal void EnterRender()
    {
        lock (_lifecycle)
        {
            ObjectDisposedException.ThrowIf(_disposed || _destroyed, this);
            _renderCount++;
        }
    }

    internal void ExitRender()
    {
        lock (_lifecycle)
        {
            _renderCount--;
            if (_renderCount == 0 && _disposed)
            {
                DestroyNow();
            }
        }
    }

    private void DestroyNow()
    {
        if (_destroyed)
        {
            return;
        }
        _destroyed = true;
        _handle?.Dispose();
        _handle = null;
    }

    public void SetPageName(string name) => Mutate(Native.nift_context_set_page_name, name);

    public void SetCurrentOutput(string path) => Mutate(Native.nift_context_set_current_output, path);

    public void SetTitle(string title) => Mutate(Native.nift_context_set_title, title);

    public void SetString(string name, string value) => Mutate(Native.nift_context_set_string, name, value);

    public void SetInt(string name, int value) => SetScalar(Native.nift_context_set_int, name, value);

    public void SetNumber(string name, double value) => SetScalar(Native.nift_context_set_number, name, value);

    public void SetBool(string name, bool value) => SetScalar(Native.nift_context_set_bool, name, value ? 1 : 0);

    public void SetJSON(string name, string json) => Mutate(Native.nift_context_set_json, name, json);

    private delegate int StringSetter(IntPtr context, IntPtr name, UIntPtr name_len, IntPtr value, UIntPtr value_len);
    private delegate int ScalarSetter(IntPtr context, IntPtr name, UIntPtr name_len, int value);
    private delegate int DoubleSetter(IntPtr context, IntPtr name, UIntPtr name_len, double value);
    private delegate int IntPtrSetter(IntPtr context, IntPtr name, UIntPtr name_len);

    private void Mutate(StringSetter setter, string name, string value)
    {
        using VarString vars = new(name, value);
        CheckStatus(setter(Handle.DangerousGetHandle(), vars.Name, vars.NameLen, vars.Value, vars.ValueLen), name);
    }

    private void Mutate(IntPtrSetter setter, string name)
    {
        using VarString vars = new(name);
        CheckStatus(setter(Handle.DangerousGetHandle(), vars.Name, vars.NameLen), name);
    }

    private void SetScalar(ScalarSetter setter, string name, int value)
    {
        using VarString vars = new(name);
        CheckStatus(setter(Handle.DangerousGetHandle(), vars.Name, vars.NameLen, value), name);
    }

    private void SetScalar(DoubleSetter setter, string name, double value)
    {
        using VarString vars = new(name);
        CheckStatus(setter(Handle.DangerousGetHandle(), vars.Name, vars.NameLen, value), name);
    }

    private static void CheckStatus(int status, string name)
    {
        if (status != NativeStatus.Ok)
        {
            throw new NiftException($"invalid binding name: {name}");
        }
    }

    private sealed class ContextHandle : SafeHandle
    {
        public ContextHandle(IntPtr handle) : base(IntPtr.Zero, ownsHandle: true) => SetHandle(handle);

        public override bool IsInvalid => handle == IntPtr.Zero;

        protected override bool ReleaseHandle()
        {
            Native.nift_context_free(handle);
            return true;
        }
    }
}
