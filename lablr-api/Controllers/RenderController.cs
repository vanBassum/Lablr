using Microsoft.AspNetCore.Mvc;

namespace Lablr.Api.Controllers;

/// <summary>
/// The render endpoints the PWA calls. The backend is the single renderer, so the
/// preview image here is the exact bitmap that prints (the job is built from the
/// same render). The frontend never rasterizes — it displays this output.
/// </summary>
[ApiController]
[Route("api/render")]
public sealed class RenderController(LabelPrintService print) : ControllerBase
{
    /// <summary>Preview PNG for a draft (1-bit — exactly what prints).</summary>
    [HttpGet("preview")]
    public IActionResult Preview(
        [FromQuery] string draftId, [FromQuery] string? templateId, [FromQuery] string? orientation)
    {
        if (string.IsNullOrWhiteSpace(draftId)) return BadRequest(new { error = "draftId required" });
        var (png, err) = print.RenderPreview(draftId, templateId, orientation);
        return png is null ? NotFound(new { error = err }) : File(png, "image/png");
    }

    /// <summary>Preview PNG for a template with sample values (Templates admin page).</summary>
    [HttpGet("template-preview")]
    public IActionResult TemplatePreview([FromQuery] string templateId, [FromQuery] string? orientation)
    {
        if (string.IsNullOrWhiteSpace(templateId)) return BadRequest(new { error = "templateId required" });
        var (png, err) = print.RenderTemplatePreview(templateId, orientation);
        return png is null ? NotFound(new { error = err }) : File(png, "image/png");
    }

    /// <summary>DYMO job bytes for a draft (for desktop WebUSB transferOut).</summary>
    [HttpGet("job")]
    public IActionResult Job(
        [FromQuery] string draftId, [FromQuery] string? templateId, [FromQuery] string? orientation,
        [FromQuery] int nudgeX = 0, [FromQuery] int nudgeY = 0)
    {
        if (string.IsNullOrWhiteSpace(draftId)) return BadRequest(new { error = "draftId required" });
        var (job, err) = print.RenderJob(draftId, templateId, orientation, nudgeX, nudgeY);
        return job is null ? NotFound(new { error = err }) : File(job, "application/octet-stream");
    }
}
