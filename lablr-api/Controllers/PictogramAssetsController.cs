using Microsoft.AspNetCore.Mvc;

namespace Lablr.Api.Controllers;

/// <summary>Serves the raw pictogram SVGs the PWA loads as images (the public asset URL).
/// Same-origin, so drawing them onto the print canvas doesn't taint it.</summary>
[ApiController]
[Route("pictograms")]
public sealed class PictogramAssetsController(ConfigService config) : ControllerBase
{
    [HttpGet("{file}")]
    public IActionResult Get(string file) =>
        config.GetPictogramSvg(file) is { } svg ? Content(svg, "image/svg+xml") : NotFound();
}
