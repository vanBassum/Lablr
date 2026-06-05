using Microsoft.AspNetCore.Mvc;

namespace Lablr.Api.Controllers;

[ApiController]
[Route("pictograms")]
public sealed class PictogramsController(ConfigService config) : ControllerBase
{
    /// <summary>Serve a pictogram SVG from the DB (same-origin → print canvas stays untainted).</summary>
    [HttpGet("{file}")]
    public IActionResult Get(string file) =>
        config.GetPictogramSvg(file) is { } svg ? Content(svg, "image/svg+xml") : NotFound();
}
