using System.Runtime.InteropServices;

namespace Nift.Interop;

/// <summary>Disposable holder of one or two UTF-8 native buffers for a C ABI call.</summary>
internal sealed class VarString : IDisposable
{
    public IntPtr Name { get; }
    public UIntPtr NameLen { get; }
    public IntPtr Value { get; }
    public UIntPtr ValueLen { get; }

    public VarString(string name, string? value = null)
    {
        (Name, NameLen) = Utf8.ToNative(name);
        (Value, ValueLen) = value is null ? (IntPtr.Zero, UIntPtr.Zero) : Utf8.ToNative(value);
    }

    public void Dispose()
    {
        Marshal.FreeHGlobal(Name);
        if (Value != IntPtr.Zero)
        {
            Marshal.FreeHGlobal(Value);
        }
    }
}
