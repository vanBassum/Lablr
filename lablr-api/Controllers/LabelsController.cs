using Microsoft.AspNetCore.Mvc;

namespace Lablr.Api.Controllers;

[ApiController]
[Route("api/labels")]
public sealed class LabelsController(ConfigService config) : ControllerBase
{
    [HttpGet]
    public IEnumerable<LabelStock> List() => config.GetLabels();

    [HttpGet("{id}")]
    public IActionResult Get(string id) =>
        config.GetLabel(id) is { } l ? Ok(l) : NotFound();

    [HttpPut("{id}")]
    public IActionResult Put(string id, [FromBody] LabelStock label)
    {
        label.Id = id; // route is authoritative
        return Ok(config.UpsertLabel(label));
    }

    [HttpDelete("{id}")]
    public IActionResult Delete(string id) =>
        config.DeleteLabel(id) ? NoContent() : NotFound();
}
