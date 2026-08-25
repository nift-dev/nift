using System.Runtime.InteropServices;
using System.Text;

namespace Nift.Interop;

/// <summary>UTF-8 marshalling helpers for the length-explicit C ABI.</summary>
internal static class Utf8
{
    /// <summary>Allocate unmanaged UTF-8 bytes for a call; caller must FreeHGlobal.</summary>
    public static (IntPtr ptr, UIntPtr len) ToNative(string value)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(value ?? "");
        IntPtr ptr = Marshal.AllocHGlobal(bytes.Length);
        if (bytes.Length > 0)
        {
            Marshal.Copy(bytes, 0, ptr, bytes.Length);
        }
        return (ptr, (UIntPtr)bytes.Length);
    }

    /// <summary>Copy a borrowed {data,length} view into a managed string.</summary>
    public static string FromNative(IntPtr data, UIntPtr length)
    {
        if (data == IntPtr.Zero)
        {
            return "";
        }
        return Marshal.PtrToStringUTF8(data, (int)length) ?? "";
    }

    public static string FromNative(NiftString value) => FromNative(value.Data, value.Length);
}
