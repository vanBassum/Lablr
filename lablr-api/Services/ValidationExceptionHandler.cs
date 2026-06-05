using Microsoft.AspNetCore.Diagnostics;

namespace Lablr.Api.Services;

/// <summary>Maps a <see cref="ConfigValidationException"/> to a 400 for REST callers.
/// (MCP surfaces thrown exceptions as tool errors on its own.)</summary>
public sealed class ValidationExceptionHandler : IExceptionHandler
{
    public async ValueTask<bool> TryHandleAsync(HttpContext ctx, Exception ex, CancellationToken ct)
    {
        if (ex is not ConfigValidationException) return false;
        ctx.Response.StatusCode = StatusCodes.Status400BadRequest;
        await ctx.Response.WriteAsJsonAsync(new { error = ex.Message }, ct);
        return true;
    }
}
