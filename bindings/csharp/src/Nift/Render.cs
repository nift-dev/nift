namespace Nift;

/// <summary>A caller-owned render source: in-memory text or a path resolved
/// against the engine root. An optional logical name gives text sources an
/// identity in diagnostics.</summary>
public readonly record struct RenderSource(string Data, bool IsPath = false, string? LogicalName = null)
{
    public static RenderSource Text(string text, string? logicalName = null)
        => new(text, false, logicalName);

    public static RenderSource Path(string path)
        => new(path, true, null);
}

/// <summary>A paginated page in a render result (pages 2..N ascending, 1-based).</summary>
public readonly record struct PaginatedPage(uint Page, string Output);

/// <summary>
/// The outcome of a render. `Ok` plus the composed output on success; on
/// failure the diagnostic carried by the C ABI (message/source/line/column).
/// Pagination holds pages 2..N; Dependencies and Requirements are root-relative
/// spellings. This mirrors the frozen Embed contract: render semantics live in
/// the result, not in the mechanical status of the call.
/// </summary>
public sealed class RenderResult
{
    public required bool Ok { get; init; }
    public required string Output { get; init; }
    public string? ErrorMessage { get; init; }
    public string? ErrorSource { get; init; }
    public ulong ErrorLine { get; init; }
    public ulong ErrorColumn { get; init; }
    public IReadOnlyList<PaginatedPage> Pagination { get; init; } = Array.Empty<PaginatedPage>();
    public IReadOnlyList<string> Dependencies { get; init; } = Array.Empty<string>();
    public IReadOnlyList<string> Requirements { get; init; } = Array.Empty<string>();

    public void ThrowIfFailed()
    {
        if (!Ok)
        {
            throw new NiftRenderException(ErrorMessage ?? "render failed", ErrorSource, ErrorLine, ErrorColumn);
        }
    }
}
