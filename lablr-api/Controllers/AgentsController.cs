using Microsoft.AspNetCore.Mvc;

namespace Lablr.Api.Controllers;

/// <summary>
/// Live print-bridge agents. The agents themselves connect over WebSocket
/// (see /agent/ws in Program.cs); this REST surface is for the PWA to list
/// them and to hand a rendered job off for relay to a specific agent.
/// </summary>
[ApiController]
[Route("api/agents")]
public sealed class AgentsController(PrintAgentRegistry registry) : ControllerBase
{
    [HttpGet]
    public IEnumerable<PrintAgentDto> List() => registry.List();

    /// <summary>Relay a rendered print job (raw bytes) to the given agent.</summary>
    [HttpPost("{id}/print")]
    public async Task<IActionResult> Print(string id, CancellationToken ct)
    {
        using var ms = new MemoryStream();
        await Request.Body.CopyToAsync(ms, ct);
        var bytes = ms.ToArray();
        if (bytes.Length == 0)
            return BadRequest(new { error = "empty body" });

        return await registry.SendAsync(id, bytes, ct) switch
        {
            PrintResult.Ok => Accepted(new { sent = bytes.Length }),
            PrintResult.PrinterNotReady => StatusCode(503, new { error = "printer not ready" }),
            _ => NotFound(new { error = "agent not connected" }),
        };
    }
}
