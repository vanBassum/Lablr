using Lablr.Api;
using Microsoft.AspNetCore.HttpOverrides;
using Microsoft.Extensions.FileProviders;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddOpenApi();
builder.Services.AddSingleton<ConfigService>();
builder.Services.AddSingleton<DraftStore>();
builder.Services.AddHostedService<DraftSweeper>();

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
        // vite port, so allow any origin in Development; otherwise use the configured
        // list. AllowAnyHeader/Method + cross-origin image reads keep the print
        // canvas untainted in dev.
        if (builder.Environment.IsDevelopment())
            policy.SetIsOriginAllowed(_ => true).AllowAnyHeader().AllowAnyMethod();
        else if (corsOrigins.Length > 0)
            policy.WithOrigins(corsOrigins).AllowAnyHeader().AllowAnyMethod();
    }));

var app = builder.Build();

// Eager-load config at startup so failures surface immediately.
var config = app.Services.GetRequiredService<ConfigService>();

app.UseForwardedHeaders();

if (app.Environment.IsDevelopment())
    app.MapOpenApi();

app.UseCors("ui");

// Serve pictogram SVGs straight from the config directory (the mount).
if (Directory.Exists(config.PictogramsDir))
{
    app.UseStaticFiles(new StaticFileOptions
    {
        FileProvider = new PhysicalFileProvider(config.PictogramsDir),
        RequestPath = "/pictograms",
    });
}

// Serve the built PWA (copied into wwwroot at image build) same-origin.
app.UseDefaultFiles();
app.UseStaticFiles();

string DraftUrl(HttpRequest req, string id)
{
    var baseUrl = app.Configuration["App:PublicBaseUrl"];
    if (string.IsNullOrEmpty(baseUrl))
        baseUrl = $"{req.Scheme}://{req.Host}{req.PathBase}/";
    if (!baseUrl.EndsWith('/')) baseUrl += "/";
    return $"{baseUrl}#/d/{id}";
}

app.MapGet("/api/health", () => Results.Ok(new { status = "ok" }));

app.MapGet("/api/config", (ConfigService c) => Results.Json(c.Config));

app.MapGet("/api/drafts", (DraftStore store) => Results.Ok(store.List()));

app.MapGet("/api/drafts/{id}", (string id, DraftStore store) =>
    store.Get(id) is { } draft ? Results.Ok(draft) : Results.NotFound());

app.MapPost("/api/drafts", (CreateDraftRequest req, ConfigService c, DraftStore store, HttpRequest http) =>
{
    if (req.Fields is null || req.Fields.Count == 0)
        return Results.BadRequest(new { error = "fields required" });

    if (!string.IsNullOrEmpty(req.TemplateId))
    {
        var template = c.Config.Templates.FirstOrDefault(t => t.Id == req.TemplateId);
        if (template is null)
            return Results.BadRequest(new { error = $"unknown template '{req.TemplateId}'" });

        var missing = template.RequiredFields
            .Where(f => !req.Fields.TryGetValue(f, out var v) || string.IsNullOrWhiteSpace(v))
            .ToList();
        if (missing.Count > 0)
            return Results.BadRequest(new { error = "missing required fields", missing });
    }

    var draft = store.Create(req.Fields, req.TemplateId);
    return Results.Ok(new { id = draft.Id, url = DraftUrl(http, draft.Id), draft });
});

// SPA fallback: client-side routes (and #/d/{id}) resolve to index.html.
app.MapFallbackToFile("index.html");

app.Run();
