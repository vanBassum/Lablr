using Microsoft.AspNetCore.Mvc;

namespace Lablr.Api.Controllers;

/// <summary>
/// Registered print bridges. Each is a persistent record with a secret token;
/// the device connects to /agent/ws with that token (see Program.cs) and its
/// live state is merged in here. The PWA manages the list and hands rendered
/// jobs off for relay to a specific bridge.
/// </summary>
[ApiController]
[Route("api/agents")]
public sealed class AgentsController(PrintAgentStore store, PrintAgentRegistry registry) : ControllerBase
{
    public sealed record CreateRequest(string Name);
    public sealed record RenameRequest(string Name);

    [HttpGet]
    public IEnumerable<PrintAgentDto> List() => store.List().Select(ToDto);

    [HttpGet("{id}")]
    public IActionResult Get(string id) =>
        store.Find(id) is { } a ? Ok(ToDto(a)) : NotFound();

    /// <summary>Register a new bridge. Returns the token once — the user copies it
    /// into the device's cloud.token setting.</summary>
    [HttpPost]
    public IActionResult Create([FromBody] CreateRequest req)
    {
        var a = store.Create(req?.Name ?? "");
        return Ok(new PrintAgentCreatedDto(a.Id, a.Name, a.Token));
    }

    [HttpPut("{id}")]
    public IActionResult Rename(string id, [FromBody] RenameRequest req) =>
        store.Rename(id, req?.Name ?? "") is { } a ? Ok(ToDto(a)) : NotFound();

    [HttpDelete("{id}")]
    public IActionResult Delete(string id)
    {
        registry.Drop(id); // close the live socket if connected
        return store.Delete(id) ? NoContent() : NotFound();
    }

    /// <summary>Relay a rendered print job (raw bytes) to the bridge.</summary>
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
            _ => NotFound(new { error = "bridge not connected" }),
        };
    }

    private PrintAgentDto ToDto(PrintAgent a)
    {
        var live = registry.GetLive(a.Id);
        var status = live is null ? "offline" : live.PrinterReady ? "ready" : "no-printer";
        return new PrintAgentDto(a.Id, a.Name, live is not null, status, live?.DeviceId, live?.LastSeen, a.CreatedAt);
    }
}
