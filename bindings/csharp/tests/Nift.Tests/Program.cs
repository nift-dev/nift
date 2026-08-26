using System.Collections.Concurrent;
using System.Text;
using Nift;

namespace Nift.Tests;

internal static class Program
{
    private static int _passed;
    private static int _failed;

    private static int Main()
    {
        Run("ABI version", TestAbiVersion);
        Run("engine new/dispose lifecycle", TestEngineLifecycle);
        Run("project-aware open + isOpen/openError", TestProjectOpen);
        Run("setRoot + composed render", TestComposedRender);
        Run("string/int/number/bool/json bindings", TestBindings);
        Run("context bindings", TestContextBindings);
        Run("context-over-engine precedence", TestContextOverEngine);
        Run("invalid engine binding name -> setup failure", TestInvalidEngineBinding);
        Run("invalid context binding name -> setup failure", TestInvalidContextBinding);
        Run("malformed JSON content render failure family", TestMalformedJsonFailure);
        Run("loader exception containment across the native boundary", TestLoaderExceptionContainment);
        Run("loader: found / not-found / missing input", TestLoader);
        Run("environment: found / not-found / error diagnostic", TestEnvironment);
        Run("pagination (three pages)", TestPagination);
        Run("dependencies + requirements", TestDependenciesRequirements);
        Run("text vs path render sources", TestRenderSources);
        Run("ThrowIfFailed", TestThrowIfFailed);
        Run("concurrent renders with loader", TestConcurrentRenders);
        Run("lifetime: repeated create/dispose", TestRepeatedCreateDispose);
        Run("delegate rooting survives GC", TestDelegateRooting);
        Run("context disposal safety", TestContextDispose);

        Console.WriteLine($"\nC# binding tests: {_passed} passed, {_failed} failed");
        return _failed == 0 ? 0 : 1;
    }

    private static void Run(string name, Action test)
    {
        try
        {
            test();
            _passed++;
            Console.WriteLine("PASS " + name);
        }
        catch (Exception ex)
        {
            _failed++;
            Console.WriteLine($"FAIL {name}: {ex.GetType().Name}: {ex.Message}");
        }
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
        {
            throw new Exception(message);
        }
    }

    private static void AssertEq<T>(T actual, T expected, string what)
    {
        if (!EqualityComparer<T>.Default.Equals(actual, expected))
        {
            throw new Exception($"{what}: expected <{expected}>, got <{actual}>");
        }
    }

    private static void AssertOk(RenderResult result, string what)
    {
        if (!result.Ok)
        {
            throw new Exception($"{what} failed: {result.ErrorMessage}");
        }
    }

    // ---- fixtures ----------------------------------------------------------

    private sealed class ProjectFixture : IDisposable
    {
        public string Root { get; }

        public ProjectFixture()
        {
            Root = Path.Combine(Path.GetTempPath(), "nift-csharp-test-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Path.Combine(Root, ".nift"));
            Directory.CreateDirectory(Path.Combine(Root, "content"));
            Directory.CreateDirectory(Path.Combine(Root, "templates"));
            Directory.CreateDirectory(Path.Combine(Root, "public"));
            Write(".nift/config.json", "{\"config\":{\"content-dir\":\"content/\",\"output-dir\":\"public/\",\"default-template\":\"templates/template.html\",\"incremental-mode\":\"modified\"}}");
            Write(".nift/tracked.json", "{\"tracked\":[{\"name\":\"blog\",\"title\":\"Blog\",\"template\":\"templates/template.html\",\"paginate\":{\"items-per-page\":1}}]}");
            Write("content/blog.html", "@item{one}@item{two}@item{three}@paginate");
            Write("content/blog.paginate.html", "<section>page $[paginate.current]/$[paginate.total]:[$[paginate.items]]</section>");
            Write("content/home.html", "<p>home</p>\n");
            Write("content/bad.json", "not json\n");
            Write("templates/template.html", "<main>$[title]</main>\n@content");
        }

        public void Write(string rel, string content)
        {
            string path = Path.Combine(Root, rel);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, content);
        }

        public void Dispose()
        {
            try
            {
                Directory.Delete(Root, recursive: true);
            }
            catch
            {
                // best-effort cleanup
            }
        }
    }

    // ---- tests -------------------------------------------------------------

    private static void TestAbiVersion()
    {
        AssertEq(NiftApi.AbiVersion, "1.0", "abi version");
    }

    private static void TestEngineLifecycle()
    {
        var engine = Engine.New();
        Assert(!engine.IsOpen(), "standalone engine should not be open");
        engine.Dispose();
        AssertEq(NiftApi.AbiVersion, "1.0", "abi still reachable");
    }

    private static void TestProjectOpen()
    {
        using var fixture = new ProjectFixture();
        var engine = Engine.Open(fixture.Root);
        Assert(engine.IsOpen(), "project engine should be open");
        AssertEq(engine.OpenError(), "", "open error empty");
        var result = engine.RenderPage("blog");
        AssertOk(result, "page render");
        engine.Dispose();
    }

    private static void TestComposedRender()
    {
        using var engine = Engine.New();
        var result = engine.Render("<p>hello</p>", "<main>@content</main>");
        AssertOk(result, "composed render");
        AssertEq(result.Output, "<main><p>hello</p></main>", "composed output");
    }

    private static void TestBindings()
    {
        using var engine = Engine.New();
        engine.SetString("s", "hello");
        engine.SetInt("i", 42);
        engine.SetNumber("n", 1.5);
        engine.SetBool("b", true);
        engine.SetJSON("v", "{\"x\":1,\"y\":\"z\"}");
        var result = engine.Render("$[s]|$[i]|$[n]|$[b]|$[v.x]|$[v.y]", "<main>@content</main>");
        AssertOk(result, "binding render");
        AssertEq(result.Output, "<main>hello|42|1.5|true|1|z</main>", "binding values");
    }

    private static void TestContextBindings()
    {
        using var engine = Engine.New();
        using var context = new Context();
        context.SetString("site", "ctx-site");
        context.SetInt("n", 7);
        context.SetJSON("v", "{\"a\":1}");
        var result = engine.Render("$[site]|$[n]|$[v.a]", "<main>@content</main>", context);
        AssertOk(result, "context render");
        AssertEq(result.Output, "<main>ctx-site|7|1</main>", "context values");
    }

    private static void TestContextOverEngine()
    {
        using var engine = Engine.New();
        engine.SetString("site", "engine");
        using var context = new Context();
        context.SetString("site", "context");
        var result = engine.Render("$[site]", "<main>@content</main>", context);
        AssertOk(result, "precedence render");
        AssertEq(result.Output, "<main>context</main>", "context must win");
    }

    private static void TestInvalidEngineBinding()
    {
        using var engine = Engine.New();
        try
        {
            engine.SetString("9bad", "x");
            throw new Exception("expected NiftException");
        }
        catch (NiftException ex)
        {
            AssertEq(ex.Message, "invalid binding name: 9bad", "engine binding diagnostic");
        }
    }

    private static void TestInvalidContextBinding()
    {
        using var context = new Context();
        try
        {
            context.SetString("9bad", "x");
            throw new Exception("expected NiftException");
        }
        catch (NiftException ex)
        {
            AssertEq(ex.Message, "invalid binding name: 9bad", "context binding diagnostic");
        }
    }

    private static void TestMalformedJsonFailure()
    {
        using var fixture = new ProjectFixture();
        using var engine = Engine.New();
        engine.SetRoot(fixture.Root);
        var result = engine.Render("@json(\"content/bad.json\", d)$[d.x]@content", "<main>@content</main>");
        Assert(!result.Ok, "malformed JSON must fail");
        Assert(result.ErrorMessage is not null && result.ErrorMessage.StartsWith("json: failed to parse content/bad.json ("),
            $"diagnostic must be in the frozen error family: {result.ErrorMessage}");
    }

    private static void TestLoader()
    {
        using var engine = Engine.New();
        engine.SetRoot("/");
        engine.SetLoader(path =>
        {
            if (path.EndsWith("/part.html"))
            {
                return HostResult.Found("<p>PART</p>");
            }
            return HostResult.NotFound();
        });
        var result = engine.Render("@input(\"part.html\")", "<main>@content</main>");
        AssertOk(result, "loader found render");
        AssertEq(result.Output, "<main><p>PART</p></main>", "loader content");

        var missing = engine.Render("@input(\"nope.html\")", "<main>@content</main>");
        Assert(!missing.Ok, "missing input must fail");
        AssertEq(missing.ErrorMessage, "@input path does not exist: nope.html", "missing input diagnostic");
    }

    private static void TestEnvironment()
    {
        using var engine = Engine.New();
        engine.SetEnvironmentProvider(name => name == "GREETING" ? HostResult.Found("hi") : HostResult.NotFound());
        var result = engine.Render("@getenv(GREETING)", "<main>@content</main>");
        AssertOk(result, "env found");
        AssertEq(result.Output, "<main>hi</main>", "env value");

        using var failing = Engine.New();
        failing.SetEnvironmentProvider(_ => HostResult.Failure("host exploded"));
        var failed = failing.Render("@getenv(GREETING)", "<main>@content</main>");
        Assert(!failed.Ok, "env error must fail");
        Assert(failed.ErrorMessage is not null && failed.ErrorMessage.Contains("host exploded"),
            $"env error diagnostic must survive inside the render diagnostic: {failed.ErrorMessage}");
    }

    private static void TestPagination()
    {
        using var fixture = new ProjectFixture();
        using var engine = Engine.Open(fixture.Root);
        var result = engine.RenderPage("blog");
        AssertOk(result, "paginated render");
        AssertEq(result.Pagination.Count, 2, "pagination count");
        AssertEq(result.Pagination[0].Page, 2u, "first pagination page number");
        AssertEq(result.Pagination[1].Page, 3u, "second pagination page number");
        Assert(result.Pagination[0].Output.Contains("page 2/3"), "pagination 2 output");
        Assert(result.Pagination[1].Output.Contains("page 3/3"), "pagination 3 output");
        AssertEq(result.Dependencies.Count, 3, "dependency count");
    }

    private static void TestDependenciesRequirements()
    {
        using var fixture = new ProjectFixture();
        fixture.Write("public/script.js", "/* placeholder */\n");
        using var engine = Engine.New();
        engine.SetRoot(fixture.Root);
        engine.SetLoader(path =>
            path.EndsWith("/part.html") ? HostResult.Found("<p>PART</p>")
            : path.EndsWith("/public/script.js") ? HostResult.Found("/* placeholder */\n")
            : HostResult.NotFound());
        using var context = new Context();
        context.SetCurrentOutput("public/blog.html");
        var result = engine.Render("@pathto(\"public/script.js\")@input(\"part.html\")", "<main>@content</main>", context);
        AssertOk(result, "dep/req render");
        Assert(result.Requirements.Contains("public/script.js"), "requirement present");
        Assert(result.Dependencies.Contains("part.html"), "dependency present");
    }

    private static void TestRenderSources()
    {
        using var fixture = new ProjectFixture();
        fixture.Write("templates/simple.html", "<main>@content</main>");
        using var engine = Engine.New();
        engine.SetRoot(fixture.Root);
        var result = engine.Render(RenderSource.Path("content/home.html"), RenderSource.Path("templates/simple.html"));
        AssertOk(result, "path-source render");
        AssertEq(result.Output, "<main><p>home</p></main>", "path-source output");
    }

    private static void TestThrowIfFailed()
    {
        using var engine = Engine.New();
        engine.SetEnvironmentProvider(_ => HostResult.Failure("boom"));
        var result = engine.Render("@getenv(GREETING)", "<main>@content</main>");
        try
        {
            result.ThrowIfFailed();
            throw new Exception("expected NiftRenderException");
        }
        catch (NiftRenderException)
        {
        }
    }

    private static void TestConcurrentRenders()
    {
        using var engine = Engine.New();
        engine.SetRoot("/");
        engine.SetLoader(path =>
            path.EndsWith("/part.html") ? HostResult.Found($"<p>{Guid.NewGuid():N}</p>") : HostResult.NotFound());
        var outputs = new ConcurrentBag<string>();
        Parallel.For(0, 64, _ =>
        {
            using var context = new Context();
            context.SetString("id", Guid.NewGuid().ToString("N"));
            var result = engine.Render("@input(\"part.html\")", "<main>@content</main>", context);
            if (!result.Ok)
            {
                throw new Exception($"concurrent render failed: {result.ErrorMessage}");
            }
            outputs.Add(result.Output);
        });
        AssertEq(outputs.Count, 64, "concurrent render count");
    }

    private static void TestRepeatedCreateDispose()
    {
        for (int i = 0; i < 200; i++)
        {
            var engine = Engine.New();
            engine.SetString("s", i.ToString());
            var result = engine.Render("$[s]", "<main>@content</main>");
            AssertOk(result, "repeated render");
            engine.Dispose();
        }
    }

    private static void TestDelegateRooting()
    {
        // The loader delegate is rooted by the engine; a local reference going
        // out of scope must not allow collection while native code may call it.
        var engine = Engine.New();
        engine.SetRoot("/");
        engine.SetLoader(path => path.EndsWith("/p.html") ? HostResult.Found("<p>ROOTED</p>") : HostResult.NotFound());
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        var result = engine.Render("@input(\"p.html\")", "<main>@content</main>");
        AssertOk(result, "rooted delegate render");
        AssertEq(result.Output, "<main><p>ROOTED</p></main>", "rooted delegate output");
        engine.Dispose();
    }

    private static void TestContextDispose()
    {
        using var engine = Engine.New();
        var context = new Context();
        context.SetString("s", "v");
        context.Dispose();
        bool threw = false;
        try
        {
            context.SetString("t", "x");
        }
        catch (ObjectDisposedException)
        {
            threw = true;
        }
        Assert(threw, "disposed context must reject use");
    }

    private static void TestLoaderExceptionContainment()
    {
        using var engine = Engine.New();
        engine.SetRoot("/");
        engine.SetLoader(_ => throw new InvalidOperationException("host exploded"));
        var result = engine.Render("@input(\"p.html\")", "<main>@content</main>");
        Assert(!result.Ok, "loader throw must fail the render");
        AssertEq(result.ErrorMessage, "host exploded", "exception diagnostic must survive exactly");

        using var failing = Engine.New();
        failing.SetEnvironmentProvider(_ => throw new InvalidOperationException("boom"));
        var failed = failing.Render("@getenv(X)", "<main>@content</main>");
        Assert(!failed.Ok, "env throw must fail the render");
        Assert(failed.ErrorMessage is not null && failed.ErrorMessage.Contains("boom"),
            $"env exception diagnostic: {failed.ErrorMessage}");
    }

}
