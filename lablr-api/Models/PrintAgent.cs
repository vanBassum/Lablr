using System.ComponentModel.DataAnnotations;
using System.ComponentModel.DataAnnotations.Schema;

namespace Lablr.Api.Models;

/// <summary>
/// A registered print bridge: a persistent record plus its secret token. The
/// device authenticates to /agent/ws with the token; live state (online,
/// printer ready, reported device id) is held separately in PrintAgentRegistry
/// and merged in for output. Persisted so a bridge's saved token keeps working
/// across backend restarts.
/// </summary>
[Table("print_agents")]
public sealed class PrintAgent
{
    [Key]
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Token { get; set; } = "";
    public DateTimeOffset CreatedAt { get; set; }
}

/// <summary>Output shape for the UI: the persisted record merged with live state.</summary>
public sealed record PrintAgentDto(
    string Id,
    string Name,
    bool Online,
    string Status,                 // "ready" | "no-printer" | "offline"
    string? DeviceId,              // hardware id the bridge reports once connected
    DateTimeOffset? LastSeen,
    DateTimeOffset CreatedAt);

/// <summary>Returned only by create — the token is shown to the user once.</summary>
public sealed record PrintAgentCreatedDto(string Id, string Name, string Token);
