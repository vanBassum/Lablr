using System.Net.WebSockets;
using System.Text;
using System.Text.Json;

namespace Lablr.Api.Services;

/// <summary>
/// Drives one agent WebSocket: reads the agent's JSON control messages
/// (<c>hello</c>, <c>status</c>) and keeps the registry up to date. Print bytes
/// flow the other way (registry -> socket) and are sent from the controller.
/// </summary>
public static class AgentSocketHandler
{
    public static async Task HandleAsync(
        WebSocket socket, PrintAgentRegistry registry, ILogger log, CancellationToken ct)
    {
        var buffer = new byte[2048];
        string? agentId = null;
        try
        {
            while (socket.State == WebSocketState.Open && !ct.IsCancellationRequested)
            {
                var msg = await ReceiveTextAsync(socket, buffer, ct);
                if (msg is null) break; // close frame

                JsonElement root;
                try { root = JsonDocument.Parse(msg).RootElement; }
                catch (JsonException) { continue; } // ignore malformed control frames

                var type = root.TryGetProperty("type", out var t) ? t.GetString() : null;
                var ready = root.TryGetProperty("printerReady", out var pr)
                            && pr.ValueKind == JsonValueKind.True;

                if (type == "hello")
                {
                    agentId = root.TryGetProperty("id", out var idEl) ? idEl.GetString() : null;
                    if (string.IsNullOrEmpty(agentId)) { continue; }
                    var name = root.TryGetProperty("name", out var n) ? n.GetString() ?? agentId : agentId;
                    registry.Register(agentId, name, ready, socket);
                    log.LogInformation("Agent connected: {Id} ({Name}), printer {Ready}",
                        agentId, name, ready ? "ready" : "absent");
                }
                else if (type == "status" && agentId is not null)
                {
                    registry.Update(agentId, ready);
                }
            }
        }
        catch (OperationCanceledException) { /* shutdown */ }
        catch (WebSocketException) { /* abrupt disconnect */ }
        catch (Exception ex) { log.LogWarning(ex, "Agent socket error ({Id})", agentId); }
        finally
        {
            if (agentId is not null)
            {
                registry.Remove(agentId, socket);
                log.LogInformation("Agent disconnected: {Id}", agentId);
            }
        }
    }

    /// <summary>Reassemble one (possibly fragmented) text message; null on close.</summary>
    private static async Task<string?> ReceiveTextAsync(WebSocket socket, byte[] buffer, CancellationToken ct)
    {
        using var ms = new MemoryStream();
        WebSocketReceiveResult result;
        do
        {
            result = await socket.ReceiveAsync(buffer, ct);
            if (result.MessageType == WebSocketMessageType.Close)
            {
                await socket.CloseAsync(WebSocketCloseStatus.NormalClosure, null, ct);
                return null;
            }
            ms.Write(buffer, 0, result.Count);
        }
        while (!result.EndOfMessage);

        return Encoding.UTF8.GetString(ms.GetBuffer(), 0, (int)ms.Length);
    }
}
