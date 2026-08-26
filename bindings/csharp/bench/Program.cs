// CP18 part B: C# binding raw render + repeated/server render workload.
using System.Diagnostics;
using Nift;

var engine = Engine.New();
engine.SetRoot("/");
engine.SetString("site", "nift");
const string page = "<p>$[site]</p>";
const string tpl = "<main>@content</main>";
const int n = 50000;
const int rounds = 3;
// Warm-up round (unreported) so the JIT settles before measuring.
engine.Render(page, tpl);
double[] rawSamples = new double[rounds];
double[] reqSamples = new double[rounds];
for (int r = 0; r < rounds; r++)
{
    var sw = Stopwatch.StartNew();
    for (int i = 0; i < n; i++) // raw: no request Context, engine-default binding
    {
        var rr = engine.Render(page, tpl);
        if (!rr.Ok) throw new Exception(rr.ErrorMessage);
    }
    rawSamples[r] = sw.Elapsed.TotalNanoseconds / n;
    sw.Restart();
    for (int i = 0; i < 1000; i++) // request-loop: fresh Context per request
    {
        using var c = new Context();
        c.SetString("who", "w");
        var rr = engine.Render(page, tpl, c);
        if (!rr.Ok) throw new Exception(rr.ErrorMessage);
    }
    reqSamples[r] = sw.ElapsedMilliseconds;
}
Array.Sort(rawSamples);
Array.Sort(reqSamples);
engine.Dispose();
Console.WriteLine($"cs raw={rawSamples[rounds / 2]:F0} ns/render request-loop={reqSamples[rounds / 2]:F0} ms/1000 rounds={rounds}");
