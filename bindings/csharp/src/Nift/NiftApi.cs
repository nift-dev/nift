using Nift.Interop;

namespace Nift;

/// <summary>Version information for the frozen Nift C ABI underneath this binding.</summary>
public static class NiftApi
{
    /// <summary>Frozen C ABI version string (e.g. "1.0").</summary>
    public static string AbiVersion => Native.AbiVersion();
}
