namespace Nift;

/// <summary>
/// Outcome of a host-provider lookup (loader or environment), mirroring the
/// frozen Embed contract: a value (possibly empty), an absence, or a host
/// failure carrying a diagnostic that the render reports verbatim.
/// </summary>
public enum HostStatus
{
    Found = 0,
    NotFound = 1,
    Error = 2,
}

/// <summary>Value / absent / error outcome of a host provider.</summary>
public readonly record struct HostResult(HostStatus Status, string Value = "", string Diagnostic = "")
{
    public static HostResult Found(string value) => new(HostStatus.Found, value);
    public static HostResult NotFound() => new(HostStatus.NotFound);
    public static HostResult Failure(string diagnostic) => new(HostStatus.Error, "", diagnostic);
}

/// <summary>Supplies the source for a path requested through the loader seam.</summary>
public delegate HostResult HostLoader(string path);

/// <summary>Supplies the value for an environment variable name.</summary>
public delegate HostResult HostEnvironment(string name);
