// CP18 part B: C# binding raw render + repeated/server render workload.
using System.Diagnostics;
using Nift;

var engine = Engine.New();
engine.SetRoot("/");
engine.SetString("site", "nift");
const string page = "<p>$[site]</p>";
const string tpl = "<main>@content</main>";
const int n = 50000;
var sw = Stopwatch.StartNew();
for (int i = 0; i < n; i++)
{
    var r = engine.Render(page, tpl);
    if (!r.Ok) throw new Exception(r.ErrorMessage);
}
double raw = sw.Elapsed.TotalNanoseconds / n;
sw.Restart();
for (int i = 0; i < 1000; i++)
{
    using var c = new Context();
    c.SetString("who", "w");
    var r = engine.Render(page, tpl, c);
    if (!r.Ok) throw new Exception(r.ErrorMessage);
}
long server = sw.ElapsedMilliseconds;
engine.Dispose();
Console.WriteLine($"cs raw={raw} ns/render server={server} ms/1000");
