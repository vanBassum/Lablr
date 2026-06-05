using Microsoft.AspNetCore.Mvc;

namespace Lablr.Api.Controllers;

[ApiController]
[Route("api/printers")]
public sealed class PrintersController(ConfigService config) : ControllerBase
{
    [HttpGet]
    public IEnumerable<Printer> List() => config.GetPrinters();

    [HttpGet("{id}")]
    public IActionResult Get(string id) =>
        config.GetPrinter(id) is { } p ? Ok(p) : NotFound();

    [HttpPut("{id}")]
    public IActionResult Put(string id, [FromBody] Printer printer)
    {
        printer.Id = id; // route is authoritative
        return Ok(config.UpsertPrinter(printer));
    }

    [HttpDelete("{id}")]
    public IActionResult Delete(string id) =>
        config.DeletePrinter(id) ? NoContent() : NotFound();
}
