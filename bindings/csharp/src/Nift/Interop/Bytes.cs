using System.Runtime.InteropServices;

namespace Nift.Interop;

internal static class Bytes
{
    /// <summary>Copy a borrowed native byte view into a managed array.</summary>
    public static byte[] FromNative(NiftBytes value)
    {
        int length = checked((int)value.Length.ToUInt64());
        if (length == 0)
        {
            return Array.Empty<byte>();
        }
        if (value.Data == IntPtr.Zero)
        {
            throw new NiftException("native byte view has null data");
        }
        byte[] bytes = new byte[length];
        Marshal.Copy(value.Data, bytes, 0, length);
        return bytes;
    }
}
