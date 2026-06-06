using System.Security.Cryptography;
using System.Text;
using Microsoft.AspNetCore.Mvc;

namespace Lablr.Api.Controllers;

/// <summary>
/// The render endpoints the PWA calls. The backend is the single renderer, so the
/// preview image here is the exact bitmap that prints (the job is built from the
/// same render). The frontend never rasterizes — it displays this output.
///
/// Previews are cached (server-side by content + a config version, and in the
/// browser via ETag/Cache-Control) so the draft grid is snappy and re-renders
/// nothing on revisit. The cache invalidates automatically when config changes.
/// </summary>
[ApiController]
[Route("api/render")]
public sealed class RenderController(LabelPrintService print, ConfigService config, RenderCache cache)
    : ControllerBase
{
    /// <summary>Preview PNG for a draft (1-bit — exactly what prints).</summary>
    [HttpGet("preview")]
    public IActionResult Preview(
        [FromQuery] string draftId, [FromQuery] string? templateId, [FromQuery] string? orientation)
    {
        if (string.IsNullOrWhiteSpace(draftId)) return BadRequest(new { error = "draftId required" });
        return CachedPng($"preview|{draftId}|{templateId}|{orientation}",
            () => print.RenderPreview(draftId, templateId, orientation));
    }

    /// <summary>Preview PNG for a template with sample values (Templates admin page).</summary>
    [HttpGet("template-preview")]
    public IActionResult TemplatePreview([FromQuery] string templateId, [FromQuery] string? orientation)
    {
        if (string.IsNullOrWhiteSpace(templateId)) return BadRequest(new { error = "templateId required" });
        return CachedPng($"tmpl|{templateId}|{orientation}",
            () => print.RenderTemplatePreview(templateId, orientation));
    }

    /// <summary>DYMO job bytes for a draft (for desktop WebUSB transferOut). Not cached.</summary>
    [HttpGet("job")]
    public IActionResult Job(
        [FromQuery] string draftId, [FromQuery] string? templateId, [FromQuery] string? orientation,
        [FromQuery] int nudgeX = 0, [FromQuery] int nudgeY = 0)
    {
        if (string.IsNullOrWhiteSpace(draftId)) return BadRequest(new { error = "draftId required" });
        var (job, err) = print.RenderJob(draftId, templateId, orientation, nudgeX, nudgeY);
        return job is null ? NotFound(new { error = err }) : File(job, "application/octet-stream");
    }

    // Serve a PNG with server-side + browser caching, keyed by content and the
    // current config version. A matching If-None-Match short-circuits to 304.
    private IActionResult CachedPng(string keyBase, Func<(byte[]? Png, string? Error)> render)
    {
        var version = config.ConfigVersion;
        var key = $"{keyBase}|v{version}";
        var etag = $"\"{Convert.ToHexString(SHA1.HashData(Encoding.UTF8.GetBytes(key)))[..16]}\"";

        Response.Headers.ETag = etag;
        Response.Headers.CacheControl = "private, max-age=60";

        if (Request.Headers.IfNoneMatch.ToString() == etag)
            return StatusCode(StatusCodes.Status304NotModified);

        var bytes = cache.TryGet(key, version);
        if (bytes is null)
        {
            var (png, err) = render();
            if (png is null) return NotFound(new { error = err });
            cache.Set(key, version, png);
            bytes = png;
        }
        return File(bytes, "image/png");
    }
}
