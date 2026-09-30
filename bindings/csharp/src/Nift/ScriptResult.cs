namespace Nift;

/// <summary>
/// The outcome of embedded script execution or evaluation. A successful result
/// has ValueJson; a failed result has ErrorMessage. Stdout and Stderr retain
/// the exact captured bytes for both outcomes.
/// </summary>
public sealed class ScriptResult
{
    public required bool Ok { get; init; }
    public string? ValueJson { get; init; }
    public string? ErrorMessage { get; init; }
    public required byte[] Stdout { get; init; }
    public required byte[] Stderr { get; init; }
}
