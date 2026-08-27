using System.Text.Json;
using Nift;

namespace Nift.AspDogfood;

/// <summary>
/// A small, genuine ASP.NET Core application using Nift Embed: a long-lived
/// Engine, repeated + concurrent requests, request-specific Context, Engine
/// defaults with Context precedence, project page/pagination renders, a loader
/// seam render, an environment/resource callback, and error-path evidence.
///
/// Nift remains the renderer; ASP.NET remains the web framework.
/// </summary>
public static class Program
{
    public static async Task Main(string[] args)
    {
        WebApplicationBuilder builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddConsole();
        WebApplication app = builder.Build();

        string siteRoot = Path.Combine(AppContext.BaseDirectory, "site");
        Engine engine = Engine.Open(siteRoot);
        // Long-lived engine defaults.
        engine.SetString("site", "Nift AspNetDogfood");
        // Environment/resource callback.
        engine.SetEnvironmentProvider(name =>
            name == "SITE_VERSION" ? HostResult.Found("1.0")
            : name == "FORCE_ERROR" ? HostResult.Failure("forced host error")
            : HostResult.NotFound());
        // Deterministic disposal on shutdown.
        app.Lifetime.ApplicationStopped.Register(() => engine.Dispose());

        app.MapGet("/", () => RenderHome(engine));
        app.MapGet("/posts", () => RenderBlog(engine));
        app.MapGet("/partial", () => RenderPartial(engine));
        app.MapGet("/concurrency", () => RenderConcurrency(engine));
        app.MapGet("/error", () => RenderError(engine));
        app.MapGet("/malformed", () => RenderMalformed(engine));

        await app.RunAsync();
    }

    private static IResult RenderHome(Engine engine)
    {
        using Context ctx = new();
        ctx.SetString("who", "world"); // Context binding, request-specific
        RenderResult result = engine.Render("home", ctx);
        if (!result.Ok)
        {
            return Results.Text($"render failed: {result.ErrorMessage}", statusCode: 500);
        }
        return Results.Text(result.Output, "text/html");
    }

    private static IResult RenderBlog(Engine engine)
    {
        using Context ctx = new();
        RenderResult result = engine.Render("blog", ctx);
        if (!result.Ok)
        {
            return Results.Text($"render failed: {result.ErrorMessage}", statusCode: 500);
        }
        return Results.Json(new
        {
            ok = result.Ok,
            output = result.Output,
            pages = result.Pagination.Select(p => new { page = p.Page, output = p.Output }),
            dependencies = result.Dependencies,
            requirements = result.Requirements,
        });
    }

    private static IResult RenderPartial(Engine engine)
    {
        // The loader seam is exercised through a dedicated standalone engine:
        // the source is served only by the callback, never read from disk.
        using Engine partialEngine = Engine.New();
        partialEngine.SetRoot("/");
        partialEngine.SetLoader(path =>
            path.EndsWith("/greeting.html") ? HostResult.Found("<p>from loader</p>\n")
            : HostResult.NotFound());
        RenderResult result = partialEngine.Render(RenderSource.Text("@input(\"greeting.html\")"), RenderSource.Text("<main>@content</main>"));
        if (!result.Ok)
        {
            return Results.Text($"render failed: {result.ErrorMessage}", statusCode: 500);
        }
        return Results.Text(result.Output, "text/html");
    }

    private static IResult RenderConcurrency(Engine engine)
    {
        int total = 32;
        var failures = 0;
        Parallel.For(0, total, i =>
        {
            using Context ctx = new();
            ctx.SetString("who", $"c{i}");
            RenderResult result = engine.Render("home", ctx);
            if (!result.Ok)
            {
                Interlocked.Increment(ref failures);
            }
        });
        return Results.Json(new { rendered = total, failures });
    }

    private static IResult RenderError(Engine engine)
    {
        // Host Error(diagnostic): the environment callback returns a failure and
        // the render reports the diagnostic verbatim.
        using Context ctx = new();
        RenderResult result = engine.Render(RenderSource.Text("@getenv(FORCE_ERROR)"), RenderSource.Text("<main>@content</main>"), ctx);
        if (result.Ok)
        {
            return Results.Text("unexpected: render succeeded", statusCode: 500);
        }
        return Results.Text($"render failed: {result.ErrorMessage}", statusCode: 500);
    }

    private static IResult RenderMalformed(Engine engine)
    {
        // Malformed JSON failure family: controlled failure, parser wording is
        // an implementation detail.
        using Context ctx = new();
        RenderResult result = engine.Render(RenderSource.Text("@json(\"content/bad.json\", d)$[d.x]"), RenderSource.Text("<main>@content</main>"), ctx);
        if (result.Ok)
        {
            return Results.Text("unexpected: render succeeded", statusCode: 500);
        }
        return Results.Text($"render failed: {result.ErrorMessage}", statusCode: 500);
    }
}
