using System.Runtime.InteropServices;

namespace Nift.Interop;

// nift_loader_callback / nift_environment_callback. Declared as delegate types
// so the managed Engine can root them as strong fields (GC safety while native
// code may still invoke them).
[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate int LoaderCallbackNative(IntPtr userData, IntPtr path, UIntPtr pathLen, ref NiftString out_);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate int EnvironmentCallbackNative(IntPtr userData, IntPtr name, UIntPtr nameLen, ref NiftString out_);

/// <summary>
/// Per-thread unmanaged scratch buffer for callback outputs.
///
/// The C ABI callback contract borrows the caller's `out` string and copies it
/// synchronously immediately after the callback returns (see c_abi.cpp
/// callback_result). A callback is therefore the only concurrent reader of its
/// scratch, and the same thread's next callback can only run after the prior
/// native copy completed (same-thread sequentiality). A per-thread scratch is
/// then provably safe and bounded: it is never "freed on the next callback"
/// across threads (the CP11-unsafe pattern), it is merely overwritten after the
/// previous same-thread use is complete. Memory is bounded by the number of
/// threads that invoke callbacks (render thread + engine pagination workers).
/// </summary>
internal static class CallbackScratch
{
    [ThreadStatic]
    private static IntPtr _buffer;
    [ThreadStatic]
    private static int _capacity;

    public static IntPtr Prepare(int neededBytes)
    {
        if (neededBytes < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(neededBytes));
        }
        if (_buffer != IntPtr.Zero && _capacity >= neededBytes)
        {
            return _buffer;
        }
        if (_buffer != IntPtr.Zero)
        {
            Marshal.FreeHGlobal(_buffer);
        }
        _capacity = Math.Max(neededBytes, 256);
        _buffer = Marshal.AllocHGlobal(_capacity);
        return _buffer;
    }
}
