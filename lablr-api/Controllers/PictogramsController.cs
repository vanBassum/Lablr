using Microsoft.AspNetCore.Mvc;

namespace Lablr.Api.Controllers;

[ApiController]
[Route("api/pictograms")]
public sealed class PictogramsController(ConfigService config) : ControllerBase
{
    [HttpGet]
    public IEnumerable<object> List() =>
        config.GetPictograms().Select(p => new { p.Name, p.Image });

    [HttpGet("{name}")]
    public IActionResult Get(string name) =>
        config.GetPictogram(name) is { } p ? Ok(new { p.Name, p.Image, p.Svg }) : NotFound();

    [HttpPut("{name}")]
    public IActionResult Put(string name, [FromBody] UpsertPictogramRequest body)
    {
        var p = config.UpsertPictogram(name, body.Svg);
        return Ok(new { p.Name, p.Image });
    }

    [HttpDelete("{name}")]
    public IActionResult Delete(string name) =>
        config.DeletePictogram(name) ? NoContent() : NotFound();
}
