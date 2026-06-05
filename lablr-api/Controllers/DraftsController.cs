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
        {
            var template = config.GetTemplate(req.TemplateId);
            if (template is null)
                return BadRequest(new { error = $"unknown template '{req.TemplateId}'" });

            var missing = template.RequiredFields
                .Where(f => !req.Fields.TryGetValue(f, out var v) || string.IsNullOrWhiteSpace(v))
                .ToList();
            if (missing.Count > 0)
                return BadRequest(new { error = "missing required fields", missing });
        }

        var draft = store.Create(req.Fields, req.TemplateId);
        return Ok(new { id = draft.Id, url = links.DraftUrl(draft.Id), draft });
    }
}
