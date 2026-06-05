using Microsoft.AspNetCore.Mvc;

namespace Lablr.Api.Controllers;

[ApiController]
[Route("api/drafts")]
public sealed class DraftsController(ConfigService config, DraftStore store, LinkService links) : ControllerBase
{
    [HttpGet]
    public IEnumerable<DraftDto> List() => store.List();

    [HttpGet("{id}")]
    public IActionResult Get(string id) =>
        store.Get(id) is { } draft ? Ok(draft) : NotFound();

    [HttpPost]
    public IActionResult Create([FromBody] CreateDraftRequest req)
    {
        if (req.Fields is null || req.Fields.Count == 0)
            return BadRequest(new { error = "fields required" });

        if (!string.IsNullOrEmpty(req.TemplateId))
            config.ValidateDraft(req.TemplateId, req.Fields); // throws -> 400 via handler

        var draft = store.Create(req.Fields, req.TemplateId);
        return Ok(new { id = draft.Id, url = links.DraftUrl(draft.Id), draft });
    }

    [HttpDelete("{id}")]
    public IActionResult Delete(string id) =>
        store.Delete(id) ? NoContent() : NotFound();
}
