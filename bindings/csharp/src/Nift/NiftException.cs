namespace Nift;

/// <summary>A mechanical failure of a C ABI call (invalid argument, project open
/// failure, rejected binding/setup operation). Render *semantics* are reported
/// through RenderResult, not this exception.</summary>
public class NiftException : Exception
{
    public NiftException(string message) : base(message) { }
}

/// <summary>A failed render carrying the frozen diagnostic (message, source,
/// line, column). Thrown only via RenderResult.ThrowIfFailed(); the result
/// itself always reports ok=false.</summary>
public class NiftRenderException : Exception
{
    public string? ErrorSource { get; }
    public ulong Line { get; }
    public ulong Column { get; }

    public NiftRenderException(string message, string? source, ulong line, ulong column)
        : base(message)
    {
        ErrorSource = source;
        Line = line;
        Column = column;
    }
}
