// P/Invoke bindings over the frozen Nift C ABI (include/nift/c_abi.h, version
// "1.0"). This layer is deliberately mechanical: all ownership, lifetime and
// diagnostic logic lives in the managed API (Engine/Context/RenderResult).
//
// The zero-`unsafe` gate is scoped to the Rust crates; this FFI interop layer
// uses IntPtr/marshal helpers (safe code) and keeps `unsafe` blocks out of the
// public API.
using System.Reflection;
using System.Runtime.InteropServices;

namespace Nift.Interop;

/// <summary>Native symbols come from libnift_c (libnift_c.so on Linux).
/// Resolution honours the NIFT_NATIVE_LIB environment variable so tests and
/// dogfood can point at a freshly built library without installing it.</summary>
internal static partial class Native
{
    public const string Lib = "libnift_c";

    static Native()
    {
        NativeLibrary.SetDllImportResolver(typeof(Native).Assembly, Resolve);
    }

    private static IntPtr Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (libraryName != Lib)
        {
            return IntPtr.Zero;
        }
        string? overridePath = Environment.GetEnvironmentVariable("NIFT_NATIVE_LIB");
        if (!string.IsNullOrEmpty(overridePath))
        {
            return NativeLibrary.Load(overridePath);
        }
        return IntPtr.Zero; // fall back to default resolution (libnift_c.so on the load path)
    }

    public static string AbiVersion()
    {
        IntPtr ptr = nift_abi_version();
        return Marshal.PtrToStringUTF8(ptr) ?? "";
    }
}

/// <summary>Borrowed UTF-8 string view {const char* data, size_t length}.</summary>
[StructLayout(LayoutKind.Sequential)]
internal struct NiftString
{
    public IntPtr Data;
    public UIntPtr Length;
}

/// <summary>Caller-owned render source {kind, data, length, logical_name, logical_name_length}.</summary>
[StructLayout(LayoutKind.Sequential)]
internal struct NiftSource
{
    public int Kind;
    public IntPtr Data;
    public UIntPtr Length;
    public IntPtr LogicalName;
    public UIntPtr LogicalNameLength;
}

internal static class NativeStatus
{
    public const int Ok = 0;
    public const int InvalidArgument = 1;
    public const int Project = 2;
    public const int NotFound = 3;
    public const int Callback = 4;
    public const int Internal = 5;
}

internal static class NativeSourceKind
{
    public const int Text = 0;
    public const int Path = 1;
}
