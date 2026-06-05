using Microsoft.AspNetCore.Mvc;

namespace Lablr.Api.Controllers;

[ApiController]
[Route("api/templates")]
public sealed class TemplatesController(ConfigService config) : ControllerBase
{
    [HttpGet]
    public IEnumerable<LabelTemplate> List() => config.GetTemplates();

    [HttpGet("{id}")]
    public IActionResult Get(string id) =>
        config.GetTemplate(id) is { } t ? Ok(t) : NotFound();

    [HttpPut("{id}")]
    public IActionResult Put(string id, [FromBody] LabelTemplate template)
    {
        template.Id = id; // route is authoritative
        return Ok(config.UpsertTemplate(template));
    }

    [HttpDelete("{id}")]
    public IActionResult Delete(string id) =>
        config.DeleteTemplate(id) ? NoContent() : NotFound();
}
