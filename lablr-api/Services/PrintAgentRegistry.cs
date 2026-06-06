using System.Collections.Concurrent;
using System.Net.WebSockets;

namespace Lablr.Api.Services;

public enum PrintResult { Ok, NotConnected, PrinterNotReady }

/// <summary>
/// In-memory registry of live print-bridge agents (ESP devices) connected over
/// WebSocket. Not persisted — an agent exists only while its socket is open.
/// The backend is a dumb relay: it forwards PWA-rendered print bytes to the
/// agent and never renders (Preview = Print stays in the PWA).
/// </summary>
public sealed class PrintAgentRegistry
{
    private sealed class Conn
    {
        public required string Id { get; init; }
        public required WebSocket Socket { get; init; }
        public string Name { get; set; } = "";
        public bool PrinterReady { get; set; }
        public DateTimeOffset ConnectedAt { get; } = DateTimeOffset.UtcNow;
        public DateTimeOffset LastSeen { get; set; } = DateTimeOffset.UtcNow;
        // A WebSocket forbids overlapping sends; serialise them.
        public readonly SemaphoreSlim SendLock = new(1, 1);
    }

    private readonly ConcurrentDictionary<string, Conn> _agents = new();

    /// <summary>Register (or replace, on reconnect) an agent's connection.</summary>
    public void Register(string id, string name, bool printerReady, WebSocket socket)
    {
        _agents[id] = new Conn
        {
            Id = id,
            Socket = socket,
            Name = string.IsNullOrWhiteSpace(name) ? id : name,
            PrinterReady = printerReady,
        };
    }

    public void Update(string id, bool printerReady)
    {
        if (_agents.TryGetValue(id, out var c))
        {
            c.PrinterReady = printerReady;
            c.LastSeen = DateTimeOffset.UtcNow;
        }
    }

    /// <summary>Remove on disconnect — only if the stored socket is still this one
    /// (a newer reconnect must not be evicted by an old socket's cleanup).</summary>
    public void Remove(string id, WebSocket socket)
    {
        if (_agents.TryGetValue(id, out var c) && ReferenceEquals(c.Socket, socket))
            _agents.TryRemove(id, out _);
    }

    public IEnumerable<PrintAgentDto> List() =>
        _agents.Values.OrderBy(c => c.Name, StringComparer.OrdinalIgnoreCase)
            .Select(ToDto).ToList();

    /// <summary>Relay raw print bytes to the agent's socket as one binary message.</summary>
    public async Task<PrintResult> SendAsync(string id, ReadOnlyMemory<byte> data, CancellationToken ct)
    {
        if (!_agents.TryGetValue(id, out var c))
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
            // Socket died mid-send; treat as gone.
            _agents.TryRemove(id, out _);
            return PrintResult.NotConnected;
        }
        finally
        {
            c.SendLock.Release();
        }
    }

    private static PrintAgentDto ToDto(Conn c) =>
        new(c.Id, c.Name, c.PrinterReady ? "ready" : "no-printer", c.ConnectedAt, c.LastSeen);
}
