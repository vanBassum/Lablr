using Microsoft.AspNetCore.Authentication.JwtBearer;

namespace Lablr.Api.Mcp;

/// <summary>
/// OAuth 2.1 protection for the MCP endpoint, federated to Authentik.
///
/// When <c>Auth:Enabled</c> is true, <c>/mcp</c> requires a Bearer JWT issued by
/// the Authentik application identified by <c>Auth:McpAppSlug</c>. An
/// unauthenticated request gets a 401 whose <c>WWW-Authenticate</c> header points
/// at the Protected Resource Metadata (RFC 9728); that kicks off the MCP client's
/// OAuth flow — e.g. ChatGPT's "Sign in", a PKCE public-client login against
/// Authentik with no client secret.
///
/// The PWA, <c>/api</c> and <c>/pictograms</c> are NOT touched here — they sit
/// behind Traefik forward-auth instead. The two <c>.well-known</c> documents plus
/// the 401 challenge are the whole client-facing OAuth surface, mirroring the
/// stack's working mealie-mcp server.
/// </summary>
public sealed record McpAuthOptions(bool Enabled, string AuthentikDomain, string McpAppSlug)
{
    public string Issuer => $"https://{AuthentikDomain}/application/o/{McpAppSlug}/";
    public string OpenIdConfig => $"{Issuer}.well-known/openid-configuration";
    public string AuthorizationEndpoint => $"https://{AuthentikDomain}/application/o/authorize/";
    public string TokenEndpoint => $"https://{AuthentikDomain}/application/o/token/";
    public string JwksUri => $"{Issuer}jwks/";
}

public static class McpAuth
{
    public static McpAuthOptions GetMcpAuthOptions(this IConfiguration cfg) => new(
        Enabled: cfg.GetValue("Auth:Enabled", false),
        AuthentikDomain: cfg["Auth:AuthentikDomain"] ?? "",
        McpAppSlug: cfg["Auth:McpAppSlug"] ?? "lablr-mcp");

    public static IServiceCollection AddMcpAuth(this IServiceCollection services, McpAuthOptions opt)
    {
        if (!opt.Enabled) return services;

        services.AddAuthentication(JwtBearerDefaults.AuthenticationScheme)
            .AddJwtBearer(o =>
            {
                // Authentik publishes per-app OIDC discovery here; this hands us
                // the signing keys (JWKS) and the expected issuer automatically.
                o.MetadataAddress = opt.OpenIdConfig;
                // Authentik sets aud to the client_id, which MCP clients don't let
                // us pin — so validate issuer + signature, not audience.
                o.TokenValidationParameters.ValidateAudience = false;
                o.Events = new JwtBearerEvents
                {
                    // RFC 9728: tell the client where the Protected Resource Metadata
                    // lives so it can discover the authorization server and sign in.
                    OnChallenge = ctx =>
                    {
                        ctx.HandleResponse();
                        var origin = $"{ctx.Request.Scheme}://{ctx.Request.Host}";
                        ctx.Response.StatusCode = StatusCodes.Status401Unauthorized;
                        ctx.Response.Headers.Append(
                            "WWW-Authenticate",
                            $"Bearer realm=\"mcp\", resource_metadata=\"{origin}/.well-known/oauth-protected-resource\"");
                        return Task.CompletedTask;
                    },
                };
            });
        services.AddAuthorization();
        return services;
    }

    /// <summary>
    /// Public OAuth discovery documents (RFC 9728 + RFC 8414). They advertise this
    /// origin as the resource and authorization server, while the real authorize/
    /// token endpoints live on Authentik (public PKCE client, no secret).
    /// </summary>
    public static void MapMcpAuthMetadata(this WebApplication app, McpAuthOptions opt)
    {
        if (!opt.Enabled) return;

        app.MapGet("/.well-known/oauth-protected-resource", (HttpRequest r) =>
        {
            var origin = $"{r.Scheme}://{r.Host}";
            return Results.Json(new
            {
                resource = origin,
                authorization_servers = new[] { origin },
                bearer_methods_supported = new[] { "header" },
            });
        });

        app.MapGet("/.well-known/oauth-authorization-server", (HttpRequest r) =>
        {
            var origin = $"{r.Scheme}://{r.Host}";
            return Results.Json(new
            {
                issuer = origin,
                authorization_endpoint = opt.AuthorizationEndpoint,
                token_endpoint = opt.TokenEndpoint,
                jwks_uri = opt.JwksUri,
                response_types_supported = new[] { "code" },
                grant_types_supported = new[] { "authorization_code" },
                code_challenge_methods_supported = new[] { "S256" },
                token_endpoint_auth_methods_supported = new[] { "none" },
                resource = origin,
            });
        });
    }
}
