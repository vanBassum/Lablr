using Microsoft.AspNetCore.Mvc;

namespace Lablr.Api.Controllers;

[ApiController]
[Route("api/config")]
public sealed class ConfigController(ConfigService config) : ControllerBase
{
    /// <summary>The full config the PWA needs: labels, templates, printers, pictogram registry.</summary>
    [HttpGet]
    public LabelConfig Get() => config.GetConfig();
}
