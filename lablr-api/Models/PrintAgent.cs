namespace Lablr.Api.Models;

/// <summary>
/// A live print-bridge agent (an ESP device connected over WebSocket). Unlike
/// <see cref="Printer"/> (a static config profile: id/name/dpi), an agent only
/// exists while its socket is open. Status is "ready" when a USB printer is
/// attached to the bridge, "no-printer" otherwise.
/// </summary>
public sealed record PrintAgentDto(
    string Id,
    string Name,
    string Status,
    DateTimeOffset ConnectedAt,
    DateTimeOffset LastSeen);
