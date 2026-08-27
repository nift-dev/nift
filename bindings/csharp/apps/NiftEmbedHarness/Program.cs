using System.Collections.Concurrent;
using System.Text.Json;

using Nift;

namespace Nift.EmbedHarness;

/// <summary>
/// Neutral shared-corpus adapter (fifth): JSON request on stdin -> JSON result
/// on stdout, implementing the same protocol as adapters/cpp-embed, rust-embed,
/// c-abi and go-embed. Loader/env seams go through the C# binding callbacks,
/// proving the C ABI callback boundary from a managed consumer.
/// </summary>
internal static class Program
{
    private static int Main()
    {
        try
        {
            using JsonDocument req = JsonDocument.Parse(Console.In.ReadToEnd());
            JsonElement root = req.RootElement;
            string rootPath = root.GetProperty("root").GetString()!;
            string mode = root.TryGetProperty("mode", out JsonElement m) ? m.GetString()! : "composed";
            string seam = root.TryGetProperty("seam", out JsonElement s) ? s.GetString()! : "-";

            Engine engine;
            using (engine = mode == "page" ? Engine.Open(rootPath) : Engine.New())
            {
                if (mode != "page")
                {
                    engine.SetRoot(rootPath);
                }
                using Context context = new();

                var loaderKeys = new ConcurrentQueue<string>();
                int exitCode = Run(root, engine, context, mode, seam, rootPath, loaderKeys);
                return exitCode;
            }
        }
        catch (Exception ex)
        {
            Emit(new { ok = false, error = "adapter exception: " + ex.Message });
            return 0;
        }
    }

    private static int Run(JsonElement root, Engine engine, Context context, string mode, string seam, string rootPath, ConcurrentQueue<string> loaderKeys)
    {
        // Engine-default bindings; a rejected binding is a controlled setup failure.
        if (root.TryGetProperty("bindings", out JsonElement bindings))
        {
            foreach (JsonProperty binding in bindings.EnumerateObject())
            {
                if (!TrySet(engine, binding))
                {
                    Emit(new { ok = false, error = $"invalid binding name: {binding.Name}" });
                    return 0;
                }
            }
        }
        // Per-render Context bindings (context-over-engine precedence).
        if (root.TryGetProperty("context_bindings", out JsonElement contextBindings))
        {
            foreach (JsonProperty binding in contextBindings.EnumerateObject())
            {
                if (!TrySet(context, binding))
                {
                    Emit(new { ok = false, error = $"invalid binding name: {binding.Name}" });
                    return 0;
                }
            }
        }

        switch (seam)
        {
            case "loader":
                engine.SetLoader(path =>
                {
                    loaderKeys.Enqueue(path);
                    return (path.EndsWith("/templates/template.html")) ? HostResult.Found("<main>@content</main>\n")
                        : path.EndsWith("/content/blog.html") ? HostResult.Found("<p>LOADER-CONTENT</p>\n")
                        : path.EndsWith("/content/post.html") ? HostResult.Found("@input(\"part.html\")\n")
                        : path.EndsWith("/content/part.html") ? HostResult.Found("<p>LOADER-PART</p>\n")
                        : HostResult.NotFound();
                });
                break;
            case "loader-error":
                engine.SetLoader(_ => HostResult.Failure("host exploded"));
                break;
            case "env":
                engine.SetEnvironmentProvider(name =>
                    name == "NIFT_ENV_A" ? HostResult.Found("alpha")
                    : name == "NIFT_ENV_B" ? HostResult.Found("beta")
                    : HostResult.NotFound());
                break;
            case "env-error":
                engine.SetEnvironmentProvider(_ => HostResult.Failure("host exploded"));
                break;
        }

        string pageName = root.TryGetProperty("page_name", out JsonElement pn) ? pn.GetString() ?? "" : "";
        string currentOutput = root.TryGetProperty("current_output", out JsonElement co) ? co.GetString() ?? "" : "";
        if (pageName.Length > 0)
        {
            context.SetPageName(pageName);
        }
        if (currentOutput.Length > 0)
        {
            context.SetCurrentOutput(currentOutput);
        }

        RenderResult result;
        switch (mode)
        {
            case "page":
                result = engine.Render(pageName, context);
                break;
            case "partial":
                result = engine.RenderText(Text(root, "page"), context);
                break;
            default:
            {
                string pagePath = root.TryGetProperty("page_path", out JsonElement pp) ? pp.GetString() ?? "" : "";
                string templatePath = root.TryGetProperty("template_path", out JsonElement tp) ? tp.GetString() ?? "" : "";
                RenderSource page = pagePath.Length > 0 ? RenderSource.Path(pagePath) : RenderSource.Text(Text(root, "page"));
                RenderSource template = templatePath.Length > 0 ? RenderSource.Path(templatePath) : RenderSource.Text(Text(root, "template"));
                result = engine.Render(page, template, context);
                break;
            }
        }

        if (!result.Ok)
        {
            // Errors carry only ok/error (no loaderKeys), matching the other
            // adapters: loaderKeys are part of the successful result.
            Emit(new { ok = false, error = result.ErrorMessage ?? "" });
            return 0;
        }

        var doc = new Dictionary<string, object?>
        {
            ["ok"] = true,
            ["output"] = result.Output,
            ["dependencies"] = result.Dependencies,
            ["requirements"] = result.Requirements,
            ["pagination"] = result.Pagination.Select(p => new { page = p.Page, output = p.Output }).ToArray(),
        };
        if (seam == "loader")
        {
            doc["loaderKeys"] = RelativeKeys(rootPath, loaderKeys);
        }
        Emit(doc);
        return 0;
    }

    private static string Text(JsonElement root, string key)
        => root.TryGetProperty(key, out JsonElement value) ? value.GetString() ?? "" : "";

    private static bool TrySet(Engine engine, JsonProperty binding)
    {
        string name = binding.Name;
        string? value = binding.Value.GetString();
        if (value is null)
        {
            return false;
        }
        try
        {
            if (value.StartsWith("json:", StringComparison.Ordinal))
            {
                engine.SetJSON(name, value["json:".Length..]);
            }
            else
            {
                engine.SetString(name, value);
            }
            return true;
        }
        catch (NiftException)
        {
            return false;
        }
    }

    private static bool TrySet(Context context, JsonProperty binding)
    {
        string name = binding.Name;
        string? value = binding.Value.GetString();
        if (value is null)
        {
            return false;
        }
        try
        {
            if (value.StartsWith("json:", StringComparison.Ordinal))
            {
                context.SetJSON(name, value["json:".Length..]);
            }
            else
            {
                context.SetString(name, value);
            }
            return true;
        }
        catch (NiftException)
        {
            return false;
        }
    }

    private static string[] RelativeKeys(string root, ConcurrentQueue<string> keys)
    {
        // Separator normalization: the engine reports loader keys with forward
        // slashes (generic_string) on every platform.
        static string Norm(string s) => s.Replace('\\', '/');
        string prefix = Norm(root).TrimEnd('/') + "/";
        var seen = new HashSet<string>();
        foreach (string key in keys)
        {
            string kn = Norm(key);
            string relative = kn.StartsWith(prefix, StringComparison.Ordinal) ? kn[prefix.Length..] : kn;
            seen.Add(relative);
        }
        return seen.OrderBy(k => k, StringComparer.Ordinal).ToArray();
    }

    private static void Emit(object doc)
    {
        Console.Out.WriteLine(JsonSerializer.Serialize(doc, new JsonSerializerOptions { WriteIndented = false }));
    }
}
