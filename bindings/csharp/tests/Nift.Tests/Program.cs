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
        Run("dispose engine during in-flight render is deferred", TestDisposeEngineDuringRender);
        Run("dispose context during in-flight render is deferred", TestDisposeContextDuringRender);
        Run("non-render operation vs Dispose: engine setter", TestSetterRacesDispose);
        Run("non-render operation vs Dispose: query", TestQueryRacesDispose);
        Run("non-render operation vs Dispose: context setter", TestContextSetterRacesDispose);
        Run("CP19 render API", TestRenderApi);

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
        var result = engine.Render("blog");
        AssertOk(result, "page render");
        engine.Dispose();
    }

    private static void TestComposedRender()
    {
        using var engine = Engine.New();
        var result = engine.Render(RenderSource.Text("<p>hello</p>"), RenderSource.Text("<main>@content</main>"));
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
        var result = engine.Render(RenderSource.Text("$[s]|$[i]|$[n]|$[b]|$[v.x]|$[v.y]"), RenderSource.Text("<main>@content</main>"));
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
        var result = engine.Render(RenderSource.Text("$[site]|$[n]|$[v.a]"), RenderSource.Text("<main>@content</main>"), context);
        AssertOk(result, "context render");
        AssertEq(result.Output, "<main>ctx-site|7|1</main>", "context values");
    }

    private static void TestContextOverEngine()
    {
        using var engine = Engine.New();
        engine.SetString("site", "engine");
        using var context = new Context();
        context.SetString("site", "context");
        var result = engine.Render(RenderSource.Text("$[site]"), RenderSource.Text("<main>@content</main>"), context);
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
        var result = engine.Render(RenderSource.Text("@json(d, \"content/bad.json\")$[d.x]@content"), RenderSource.Text("<main>@content</main>"));
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
        var result = engine.Render(RenderSource.Text("@input(\"part.html\")"), RenderSource.Text("<main>@content</main>"));
        AssertOk(result, "loader found render");
        AssertEq(result.Output, "<main><p>PART</p></main>", "loader content");

        var missing = engine.Render(RenderSource.Text("@input(\"nope.html\")"), RenderSource.Text("<main>@content</main>"));
        Assert(!missing.Ok, "missing input must fail");
        AssertEq(missing.ErrorMessage, "@input path does not exist: nope.html", "missing input diagnostic");
    }

    private static void TestEnvironment()
    {
        using var engine = Engine.New();
        engine.SetEnvironmentProvider(name => name == "GREETING" ? HostResult.Found("hi") : HostResult.NotFound());
        var result = engine.Render(RenderSource.Text("@getenv(GREETING)"), RenderSource.Text("<main>@content</main>"));
        AssertOk(result, "env found");
        AssertEq(result.Output, "<main>hi</main>", "env value");

        using var failing = Engine.New();
        failing.SetEnvironmentProvider(_ => HostResult.Failure("host exploded"));
        var failed = failing.Render(RenderSource.Text("@getenv(GREETING)"), RenderSource.Text("<main>@content</main>"));
        Assert(!failed.Ok, "env error must fail");
        Assert(failed.ErrorMessage is not null && failed.ErrorMessage.Contains("host exploded"),
            $"env error diagnostic must survive inside the render diagnostic: {failed.ErrorMessage}");
    }

    private static void TestPagination()
    {
        using var fixture = new ProjectFixture();
        using var engine = Engine.Open(fixture.Root);
        var result = engine.Render("blog");
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
        var result = engine.Render(RenderSource.Text("@pathto(\"public/script.js\")@input(\"part.html\")"), RenderSource.Text("<main>@content</main>"), context);
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
        var result = engine.Render(RenderSource.Text("@getenv(GREETING)"), RenderSource.Text("<main>@content</main>"));
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
            var result = engine.Render(RenderSource.Text("@input(\"part.html\")"), RenderSource.Text("<main>@content</main>"), context);
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
            var result = engine.Render(RenderSource.Text("$[s]"), RenderSource.Text("<main>@content</main>"));
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
        var result = engine.Render(RenderSource.Text("@input(\"p.html\")"), RenderSource.Text("<main>@content</main>"));
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
        var result = engine.Render(RenderSource.Text("@input(\"p.html\")"), RenderSource.Text("<main>@content</main>"));
        Assert(!result.Ok, "loader throw must fail the render");
        AssertEq(result.ErrorMessage, "host exploded", "exception diagnostic must survive exactly");

        using var failing = Engine.New();
        failing.SetEnvironmentProvider(_ => throw new InvalidOperationException("boom"));
        var failed = failing.Render(RenderSource.Text("@getenv(X)"), RenderSource.Text("<main>@content</main>"));
        Assert(!failed.Ok, "env throw must fail the render");
        Assert(failed.ErrorMessage is not null && failed.ErrorMessage.Contains("boom"),
            $"env exception diagnostic: {failed.ErrorMessage}");
    }


    private static void TestDisposeEngineDuringRender()
    {
        var engine = Engine.New();
        engine.SetRoot("/");
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        engine.SetLoader(path =>
        {
            entered.Set();
            release.Wait(TimeSpan.FromSeconds(10));
            return path.EndsWith("/p.html") ? HostResult.Found("<p>PART</p>") : HostResult.NotFound();
        });
        RenderResult? result = null;
        Exception? renderEx = null;
        var thread = new Thread(() =>
        {
            try { result = engine.Render(RenderSource.Text("@input(\"p.html\")"), RenderSource.Text("<main>@content</main>")); }
            catch (Exception ex) { renderEx = ex; }
        });
        thread.Start();
        Assert(entered.Wait(TimeSpan.FromSeconds(10)), "render never entered native execution");
        engine.Dispose(); // must defer native destruction
        bool rejected = false;
        try { engine.Render(RenderSource.Text("x"), RenderSource.Text("y")); }
        catch (ObjectDisposedException) { rejected = true; }
        Assert(rejected, "disposed engine must reject a new render");
        release.Set();
        thread.Join();
        Assert(renderEx is null, "render threw: " + renderEx);
        AssertOk(result!, "dispose-during-render result");
        AssertEq(result!.Output, "<main><p>PART</p></main>", "dispose-during-render output");
        engine.Dispose(); // idempotent
    }

    private static void TestDisposeContextDuringRender()
    {
        var engine = Engine.New();
        engine.SetRoot("/");
        var ctx = new Context();
        ctx.SetString("s", "ctx");
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        engine.SetLoader(path =>
        {
            entered.Set();
            release.Wait(TimeSpan.FromSeconds(10));
            return path.EndsWith("/p.html") ? HostResult.Found("<b>$[s]</b>") : HostResult.NotFound();
        });
        RenderResult? result = null;
        Exception? renderEx = null;
        var thread = new Thread(() =>
        {
            try { result = engine.Render(RenderSource.Text("@input(\"p.html\")"), RenderSource.Text("<main>@content</main>"), ctx); }
            catch (Exception ex) { renderEx = ex; }
        });
        thread.Start();
        Assert(entered.Wait(TimeSpan.FromSeconds(10)), "render never entered native execution");
        ctx.Dispose(); // must defer native context destruction
        release.Set();
        thread.Join();
        Assert(renderEx is null, "render threw: " + renderEx);
        AssertOk(result!, "dispose-during-render result");
        AssertEq(result!.Output, "<main><b>ctx</b></main>", "dispose-during-render output");
        engine.Dispose();
    }


    private static void TestSetterRacesDispose()
    {
        var engine = Engine.New();
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        Nift.Engine.NativeOpTestHook = () => { entered.Set(); release.Wait(TimeSpan.FromSeconds(10)); };
        try
        {
            Exception? opEx = null;
            var thread = new Thread(() => { try { engine.SetString("s", "x"); } catch (Exception ex) { opEx = ex; } });
            thread.Start();
            Assert(entered.Wait(TimeSpan.FromSeconds(10)), "setter never entered the guarded section");
            bool disposedWhileOp = false;
            var disposeThread = new Thread(() => { engine.Dispose(); disposedWhileOp = true; });
            disposeThread.Start();
            Thread.Sleep(100);
            Assert(!disposedWhileOp, "Dispose must block while an admitted operation holds the lifecycle mutex");
            release.Set();
            thread.Join();
            Assert(opEx is null, "admitted setter threw: " + opEx);
            disposeThread.Join();
            bool rejected = false;
            try { engine.SetString("s", "y"); } catch (ObjectDisposedException) { rejected = true; }
            Assert(rejected, "disposed engine must reject a new setter");
        }
        finally { Nift.Engine.NativeOpTestHook = null; }
    }

    private static void TestQueryRacesDispose()
    {
        var engine = Engine.New();
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        Nift.Engine.NativeOpTestHook = () => { entered.Set(); release.Wait(TimeSpan.FromSeconds(10)); };
        try
        {
            bool? query = null;
            var thread = new Thread(() => { query = engine.IsOpen(); });
            thread.Start();
            Assert(entered.Wait(TimeSpan.FromSeconds(10)), "query never entered the guarded section");
            bool disposedWhileOp = false;
            var disposeThread = new Thread(() => { engine.Dispose(); disposedWhileOp = true; });
            disposeThread.Start();
            Thread.Sleep(100);
            Assert(!disposedWhileOp, "Dispose must block while an admitted query holds the lifecycle mutex");
            release.Set();
            thread.Join();
            disposeThread.Join();
            Assert(query == false, "admitted IsOpen should report false");
            Assert(disposedWhileOp, "Dispose should complete after the query");
        }
        finally { Nift.Engine.NativeOpTestHook = null; }
    }

    private static void TestContextSetterRacesDispose()
    {
        var ctx = new Context();
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        Nift.Context.NativeOpTestHook = () => { entered.Set(); release.Wait(TimeSpan.FromSeconds(10)); };
        try
        {
            Exception? opEx = null;
            var thread = new Thread(() => { try { ctx.SetString("s", "x"); } catch (Exception ex) { opEx = ex; } });
            thread.Start();
            Assert(entered.Wait(TimeSpan.FromSeconds(10)), "context setter never entered the guarded section");
            bool disposedWhileOp = false;
            var disposeThread = new Thread(() => { ctx.Dispose(); disposedWhileOp = true; });
            disposeThread.Start();
            Thread.Sleep(100);
            Assert(!disposedWhileOp, "Context.Dispose must block while an admitted operation holds the lifecycle mutex");
            release.Set();
            thread.Join();
            Assert(opEx is null, "admitted context setter threw: " + opEx);
            disposeThread.Join();
            bool rejected = false;
            try { ctx.SetString("s", "y"); } catch (ObjectDisposedException) { rejected = true; }
            Assert(rejected, "disposed context must reject a new setter");
        }
        finally { Nift.Context.NativeOpTestHook = null; }
    }


    private static void TestRenderApi()
    {
        // Tracked-page render by name.
        string root = ScaffoldProject();
        using var engine = Engine.Open(root);
        Assert(engine.IsOpen(), "project opens");
        var byName = engine.Render("about");
        Assert(byName.Ok && byName.Output == "<main><p>about</p></main>", $"render(name): {byName.ErrorMessage}");
        var unknown = engine.Render("no-such-page");
        Assert(!unknown.Ok && unknown.ErrorMessage is not null && unknown.ErrorMessage.Contains("unknown"),
            $"unknown tracked name must be a controlled unknown-page error: {unknown.ErrorMessage}");
        using var ctx = new Context();
        ctx.SetJSON("product", "{\"name\":\"headphones\"}");
        var withCtx = engine.Render("products/headphones", ctx);
        Assert(withCtx.Ok && withCtx.Output == "<main><h1>headphones</h1></main>",
            $"render(name, ctx): {withCtx.ErrorMessage}");

        // render_path: always a filesystem path; missing path is an error.
        var path = engine.RenderPath(System.IO.Path.Combine(root, "content", "about.html"));
        Assert(path.Ok && path.Output == "<p>about</p>", $"render_path(existing): {path.ErrorMessage}");
        var missingPath = engine.RenderPath(System.IO.Path.Combine(root, "nope.html"));
        Assert(!missingPath.Ok, "render_path(missing) must be a controlled error");
        Assert(missingPath.Output != "<p>about</p>", "missing path must not be reinterpreted as the file content");

        // render_text: never checks the filesystem.
        var text = engine.RenderText("<p>literal</p>");
        Assert(text.Ok && text.Output == "<p>literal</p>", $"render_text: {text.ErrorMessage}");
        string namesAFile = System.IO.Path.Combine(root, "content", "about.html");
        var textOfPath = engine.RenderText(namesAFile);
        Assert(textOfPath.Ok && textOfPath.Output == namesAFile, "render_text must not resolve its argument as a file path");

        // Omitted context == fresh empty context; no request-state reuse.
        var noCtx = engine.RenderText("$[x]");
        using var ctx2 = new Context();
        ctx2.SetString("x", "value");
        var withCtx2 = engine.RenderText("$[x]", ctx2);
        Assert(withCtx2.Ok && withCtx2.Output == "value", $"render_text(text, ctx): {withCtx2.ErrorMessage}");
        Assert(noCtx.Ok && noCtx.Output == "$[x]", "no-context render must not reuse request state");

        // Typed composition path/path, text/text, mixed.
        string tplPath = System.IO.Path.Combine(root, "templates", "template.html");
        var pp = engine.Render(RenderSource.Path(System.IO.Path.Combine(root, "content", "about.html")),
                               RenderSource.Path(tplPath));
        Assert(pp.Ok && pp.Output == "<main><p>about</p></main>", $"path/path: {pp.ErrorMessage}");
        var tt = engine.Render(RenderSource.Text("<p>hi</p>"), RenderSource.Text("<main>@content</main>"));
        Assert(tt.Ok && tt.Output == "<main><p>hi</p></main>", $"text/text: {tt.ErrorMessage}");
        var mixed = engine.Render(RenderSource.Text("<p>mixed</p>"), RenderSource.Path(tplPath));
        Assert(mixed.Ok && mixed.Output == "<main><p>mixed</p></main>", $"mixed: {mixed.ErrorMessage}");

        System.IO.Directory.Delete(root, true);
    }

    private static string ScaffoldProject()
    {
        string root = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "nift-cp19-cs-" + Guid.NewGuid().ToString("N"));
        System.IO.Directory.CreateDirectory(System.IO.Path.Combine(root, ".nift"));
        System.IO.Directory.CreateDirectory(System.IO.Path.Combine(root, "content", "products"));
        System.IO.Directory.CreateDirectory(System.IO.Path.Combine(root, "templates"));
        System.IO.File.WriteAllText(System.IO.Path.Combine(root, ".nift", "config.json"),
            "{\"config\":{\"content-dir\":\"content/\",\"content-ext\":\".html\",\"output-dir\":\"public/\",\"output-ext\":\".html\",\"default-template\":\"templates/template.html\",\"incremental-mode\":\"modified\"}}");
        System.IO.File.WriteAllText(System.IO.Path.Combine(root, ".nift", "tracked.json"),
            "{\"tracked\":[{\"name\":\"/\",\"title\":\"Home\",\"template\":\"templates/template.html\"},{\"name\":\"about\",\"title\":\"About\",\"template\":\"templates/template.html\"},{\"name\":\"products/headphones\",\"title\":\"Headphones\",\"template\":\"templates/template.html\"}]}");
        System.IO.File.WriteAllText(System.IO.Path.Combine(root, "templates", "template.html"), "<main>@content</main>");
        System.IO.File.WriteAllText(System.IO.Path.Combine(root, "content", "index.html"), "<p>home</p>");
        System.IO.File.WriteAllText(System.IO.Path.Combine(root, "content", "about.html"), "<p>about</p>");
        System.IO.File.WriteAllText(System.IO.Path.Combine(root, "content", "products", "headphones.html"), "<h1>$[product.name]</h1>");
        return root;
    }

}
