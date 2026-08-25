using System.Runtime.InteropServices;

namespace Nift.Interop;

/// <summary>
/// Disposable holder building caller-owned NiftSource values for a render call.
/// Keeps the UTF-8 byte buffers alive for the duration of the native call.
/// </summary>
internal sealed class NativeSources : IDisposable
{
    public NiftSource Page;
    public NiftSource Template;
    public NiftSource Partial;

    private readonly List<VarString> _buffers = new();

    public NativeSources(RenderSource page, RenderSource template)
    {
        Page = Build(page, ref _pageNameBuffer);
        Template = Build(template, ref _templateNameBuffer);
    }

    public NativeSources(RenderSource partial)
    {
        Partial = Build(partial, ref _partialNameBuffer);
    }

    public void Dispose()
    {
        foreach (VarString buffer in _buffers)
        {
            buffer.Dispose();
        }
    }

    private NiftSource Build(RenderSource source, ref VarString? nameBuffer)
    {
        string data = source.Data;
        VarString buffer = new(data);
        _buffers.Add(buffer);

        VarString? logical = null;
        if (source.LogicalName is not null)
        {
            logical = new VarString(source.LogicalName);
            _buffers.Add(logical);
        }

        return new NiftSource
        {
            Kind = source.IsPath ? NativeSourceKind.Path : NativeSourceKind.Text,
            Data = buffer.Name,
            Length = buffer.NameLen,
            LogicalName = logical?.Name ?? IntPtr.Zero,
            LogicalNameLength = logical?.NameLen ?? UIntPtr.Zero,
        };
    }

    private VarString? _pageNameBuffer;
    private VarString? _templateNameBuffer;
    private VarString? _partialNameBuffer;
}
