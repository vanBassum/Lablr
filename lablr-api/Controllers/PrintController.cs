using Microsoft.AspNetCore.Mvc;

namespace Lablr.Api.Controllers;

/// <summary>
/// Server-side print: render a draft and relay it to a bridge. Same pipeline the
/// MCP print_draft tool uses (LabelPrintService) — exposed over REST for the UI
/// and for testing the headless render path.
/// </summary>
[ApiController]
[Route("api/print")]
public sealed class PrintController(LabelPrintService print) : ControllerBase
{
    public sealed record PrintDraftRequest(
        string DraftId, string? TemplateId, string? AgentId,
        string? Orientation = null, int NudgeX = 0, int NudgeY = 0);

    [HttpPost("draft")]
    public async Task<IActionResult> Draft([FromBody] PrintDraftRequest req, CancellationToken ct)
    {
        if (req is null || string.IsNullOrWhiteSpace(req.DraftId))
            return BadRequest(new { error = "draftId required" });

        var r = await print.PrintDraftAsync(
            req.DraftId, req.TemplateId, req.AgentId, req.Orientation, req.NudgeX, req.NudgeY, ct);
        return r.Ok
            ? Accepted(new { printer = r.AgentId, bytes = r.Bytes })
            : StatusCode(409, new { error = r.Error });
    }
}
