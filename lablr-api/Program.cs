using Microsoft.AspNetCore.HttpOverrides;
using Microsoft.EntityFrameworkCore;

var builder = WebApplication.CreateBuilder(args);

// SQLite config store. Db:Path is a file on a mounted volume in prod.
var dbPath = builder.Configuration["Db:Path"] ?? "lablr.db";
if (!Path.IsPathRooted(dbPath))
    dbPath = Path.GetFullPath(dbPath, builder.Environment.ContentRootPath);
builder.Services.AddDbContextFactory<LablrDbContext>(o => o.UseSqlite($"Data Source={dbPath}"));

builder.Services.AddControllers();
builder.Services.AddProblemDetails();
builder.Services.AddExceptionHandler<ValidationExceptionHandler>();
builder.Services.AddOpenApi();
builder.Services.AddHttpContextAccessor();
builder.Services.AddSingleton<ConfigService>();
builder.Services.AddSingleton<DraftStore>();
builder.Services.AddSingleton<LinkService>();
builder.Services.AddSingleton<PrintAgentStore>();
builder.Services.AddSingleton<PrintAgentRegistry>();
builder.Services.AddSingleton<LabelRenderer>();
builder.Services.AddSingleton<LabelPrintService>();
builder.Services.AddHostedService<DraftSweeper>();

// MCP server (Streamable HTTP at /mcp): an AI reads + authors config and creates drafts.
builder.Services.AddMcpServer().WithHttpTransport().WithTools<LabelTools>();

// Optional OAuth 2.1 protection for /mcp, federated to Authentik (see Mcp/McpAuth.cs).
// Disabled by default so local/dev runs need no IdP; prod sets Auth__* env vars.
var mcpAuth = builder.Configuration.GetMcpAuthOptions();
builder.Services.AddMcpAuth(mcpAuth);

// Behind Traefik (TLS terminated upstream): trust forwarded scheme/host so
// request-derived URLs (draft deep links) are correct https://public-host links.
builder.Services.Configure<ForwardedHeadersOptions>(o =>
{
    o.ForwardedHeaders = ForwardedHeaders.XForwardedProto | ForwardedHeaders.XForwardedHost;
    o.KnownNetworks.Clear();
    o.KnownProxies.Clear();
});

var corsOrigins = builder.Configuration.GetSection("Cors:AllowedOrigins").Get<string[]>() ?? [];
builder.Services.AddCors(options =>
    options.AddPolicy("ui", policy =>
    {
        // Prod is same-origin (UI served by this app). Dev UI runs on a separate
        // vite port, so allow any origin in Development; otherwise use the configured list.
        if (builder.Environment.IsDevelopment())
            policy.SetIsOriginAllowed(_ => true).AllowAnyHeader().AllowAnyMethod();
        else if (corsOrigins.Length > 0)
            policy.WithOrigins(corsOrigins).AllowAnyHeader().AllowAnyMethod();
    }));

var app = builder.Build();

// Eager-init: creates + seeds the SQLite DB at startup so failures surface now.
app.Services.GetRequiredService<ConfigService>();

app.UseExceptionHandler(); // maps ConfigValidationException -> 400
app.UseForwardedHeaders();
app.UseWebSockets();

if (app.Environment.IsDevelopment())
    app.MapOpenApi();

app.UseCors("ui");

if (mcpAuth.Enabled)
{
    app.UseAuthentication();
    app.UseAuthorization();
}

// Public OAuth discovery docs — the MCP client reads these to know how to sign in.
app.MapMcpAuthMetadata(mcpAuth);

// Serve the built PWA (copied into wwwroot at image build) same-origin.
app.UseDefaultFiles();
app.UseStaticFiles();

// MCP at /mcp — gated by Authentik OAuth when enabled; the PWA/api are gated
// separately by Traefik forward-auth, so they are not required here.
var mcp = app.MapMcp("/mcp");
if (mcpAuth.Enabled)
    mcp.RequireAuthorization();

// Print bridges connect here (wss in prod, terminated at Traefik). The device
// presents its per-printer token (?token= or Bearer); we resolve it to a
// registered agent record. Must be exempt from interactive forward-auth
// upstream — devices authenticate with the token, not a browser login.
app.Map("/agent/ws", async (HttpContext ctx, PrintAgentStore store, PrintAgentRegistry registry, ILoggerFactory lf) =>
{
    if (!ctx.WebSockets.IsWebSocketRequest)
    {
        ctx.Response.StatusCode = StatusCodes.Status400BadRequest;
        return;
    }
    var auth = ctx.Request.Headers.Authorization.ToString();
    var token = auth.StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase)
        ? auth["Bearer ".Length..].Trim()
        : ctx.Request.Query["token"].ToString();

    var agent = store.FindByToken(token);
    if (agent is null)
    {
        ctx.Response.StatusCode = StatusCodes.Status401Unauthorized;
        return;
    }

    using var socket = await ctx.WebSockets.AcceptWebSocketAsync();
    await AgentSocketHandler.HandleAsync(
        socket, registry, agent.Id, lf.CreateLogger("Agent"), ctx.RequestAborted);
});

// REST endpoints (Controllers/): config, drafts, pictograms, health, agents.
app.MapControllers();

// SPA fallback: client-side routes (and #/d/{id}) resolve to index.html.
app.MapFallbackToFile("index.html");

app.Run();
