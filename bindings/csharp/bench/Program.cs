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
double rawBest = double.MaxValue, reqBest = double.MaxValue;
for (int r = 0; r < rounds; r++)
{
    var sw = Stopwatch.StartNew();
    for (int i = 0; i < n; i++) // raw: no request Context, engine-default binding
    {
        var rr = engine.Render(page, tpl);
        if (!rr.Ok) throw new Exception(rr.ErrorMessage);
    }
    double raw = sw.Elapsed.TotalNanoseconds / n;
    if (raw < rawBest) rawBest = raw;
    sw.Restart();
    for (int i = 0; i < 1000; i++) // request-loop: fresh Context per request
    {
        using var c = new Context();
        c.SetString("who", "w");
        var rr = engine.Render(page, tpl, c);
        if (!rr.Ok) throw new Exception(rr.ErrorMessage);
    }
    double req = sw.ElapsedMilliseconds;
    if (req < reqBest) reqBest = req;
}
engine.Dispose();
Console.WriteLine($"cs raw={rawBest:F0} ns/render request-loop={reqBest:F0} ms/1000 rounds={rounds}");
