using System.Collections.Concurrent;
using System.Net.WebSockets;

namespace Lablr.Api.Services;

public enum PrintResult { Ok, NotConnected, PrinterNotReady }

/// <summary>Live state of a connected bridge, merged into the persisted record for output.</summary>
public sealed record AgentLive(bool PrinterReady, string? DeviceId, DateTimeOffset LastSeen);

/// <summary>
/// Tracks the live WebSocket connections of registered print bridges, keyed by
/// the persistent agent id (resolved from the device's token at connect time).
/// The backend is a dumb relay: it forwards PWA-rendered bytes to the bridge and
/// never renders.
/// </summary>
public sealed class PrintAgentRegistry
{
    private sealed class Conn
    {
        public required string AgentId { get; init; }
        public required WebSocket Socket { get; init; }
        public bool PrinterReady { get; set; }
        public string? DeviceId { get; set; }
        public DateTimeOffset LastSeen { get; set; } = DateTimeOffset.UtcNow;
        public readonly SemaphoreSlim SendLock = new(1, 1);
    }

    private readonly ConcurrentDictionary<string, Conn> _live = new();

    public void SetOnline(string agentId, WebSocket socket, string? deviceId, bool printerReady)
    {
        _live[agentId] = new Conn
        {
            AgentId = agentId,
            Socket = socket,
            DeviceId = deviceId,
            PrinterReady = printerReady,
        };
    }

    public void Update(string agentId, bool printerReady)
    {
        if (_live.TryGetValue(agentId, out var c))
        {
            c.PrinterReady = printerReady;
            c.LastSeen = DateTimeOffset.UtcNow;
        }
    }

    public void SetOffline(string agentId, WebSocket socket)
    {
        if (_live.TryGetValue(agentId, out var c) && ReferenceEquals(c.Socket, socket))
            _live.TryRemove(agentId, out _);
    }

    public AgentLive? GetLive(string agentId) =>
        _live.TryGetValue(agentId, out var c)
            ? new AgentLive(c.PrinterReady, c.DeviceId, c.LastSeen)
            : null;

    /// <summary>Force a connection closed (e.g. when the record is deleted).</summary>
    public void Drop(string agentId)
    {
        if (_live.TryRemove(agentId, out var c))
            _ = c.Socket.CloseAsync(WebSocketCloseStatus.NormalClosure, "removed", CancellationToken.None);
    }

    /// <summary>Relay raw print bytes to the agent's socket as one binary message.</summary>
    public async Task<PrintResult> SendAsync(string agentId, ReadOnlyMemory<byte> data, CancellationToken ct)
    {
        if (!_live.TryGetValue(agentId, out var c))
            return PrintResult.NotConnected;
        if (!c.PrinterReady)
            return PrintResult.PrinterNotReady;

        await c.SendLock.WaitAsync(ct);
        try
        {
            await c.Socket.SendAsync(data, WebSocketMessageType.Binary, endOfMessage: true, ct);
            c.LastSeen = DateTimeOffset.UtcNow;
            return PrintResult.Ok;
        }
        catch (Exception)
        {
            _live.TryRemove(agentId, out _); // socket dead
            return PrintResult.NotConnected;
        }
        finally
        {
            c.SendLock.Release();
        }
    }
}
