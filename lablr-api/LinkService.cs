namespace Lablr.Api;

/// <summary>Builds PWA deep links. Same-origin in prod (derived from the request),
/// or App:PublicBaseUrl when set (e.g. dev, where the UI is on a separate port).</summary>
public sealed class LinkService(IHttpContextAccessor accessor, IConfiguration cfg)
{
    public string DraftUrl(string id)
    {
        var baseUrl = cfg["App:PublicBaseUrl"];
        if (string.IsNullOrEmpty(baseUrl))
        {
            var req = accessor.HttpContext!.Request;
            baseUrl = $"{req.Scheme}://{req.Host}{req.PathBase}/";
        }
        if (!baseUrl.EndsWith('/')) baseUrl += "/";
        return $"{baseUrl}#/d/{id}";
    }
}
