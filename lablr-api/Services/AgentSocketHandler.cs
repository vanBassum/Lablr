using System.Net.WebSockets;
using System.Text;
using System.Text.Json;

namespace Lablr.Api.Services;

/// <summary>
/// Drives one agent WebSocket. The agent id has already been resolved from the
/// device's token (see Program.cs). We read the device's JSON control messages
/// (<c>hello</c>, <c>status</c>) to mark it online and track printer readiness.
/// Print bytes flow the other way (registry -> socket), sent from the controller.
/// </summary>
public static class AgentSocketHandler
{
    public static async Task HandleAsync(
        WebSocket socket, PrintAgentRegistry registry, string agentId, ILogger log, CancellationToken ct)
    {
        var buffer = new byte[2048];
        // Mark online immediately; hello refines the reported details.
        registry.SetOnline(agentId, socket, deviceId: null, printerReady: false);
        log.LogInformation("Agent online: {Id}", agentId);
        try
        {
            while (socket.State == WebSocketState.Open && !ct.IsCancellationRequested)
            {
                var msg = await ReceiveTextAsync(socket, buffer, ct);
                if (msg is null) break; // close frame

                JsonElement root;
                try { root = JsonDocument.Parse(msg).RootElement; }
                catch (JsonException) { continue; }

                var type = root.TryGetProperty("type", out var t) ? t.GetString() : null;
                var ready = root.TryGetProperty("printerReady", out var pr)
                            && pr.ValueKind == JsonValueKind.True;

                if (type == "hello")
                {
                    var deviceId = root.TryGetProperty("id", out var idEl) ? idEl.GetString() : null;
                    registry.SetOnline(agentId, socket, deviceId, ready);
                    log.LogInformation("Agent hello: {Id} device={Device} printer {Ready}",
                        agentId, deviceId, ready ? "ready" : "absent");
                }
                else if (type == "status")
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
            registry.SetOffline(agentId, socket);
            log.LogInformation("Agent offline: {Id}", agentId);
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
