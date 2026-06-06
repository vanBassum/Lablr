using System.Net.Http.Json;

namespace Lablr.Api.Services;

public sealed record PrintDraftResult(bool Ok, string? Error = null, string? AgentId = null, int Bytes = 0);

/// <summary>
/// Headless print pipeline: resolve a draft to its template/stock/printer, render
/// it via the lablr-render service (which runs the PWA's exact render code — one
/// renderer), and relay the bytes to a connected bridge. Used by the AI (MCP
/// print_draft) and the REST /api/print/draft endpoint.
/// </summary>
public sealed class LabelPrintService(
    ConfigService config, DraftStore drafts, PrintAgentStore agents,
    PrintAgentRegistry registry, IHttpClientFactory httpFactory, IConfiguration cfg)
{
    private readonly string _renderUrl =
        (cfg["Render:Url"] ?? "http://lablr-render:8090").TrimEnd('/');

    public async Task<PrintDraftResult> PrintDraftAsync(
        string draftId, string? templateId, string? agentId, CancellationToken ct)
    {
        var draft = drafts.Get(draftId);
        if (draft is null)
            return new(false, $"Unknown draft '{draftId}'.");

        var template = ResolveTemplate(templateId ?? draft.TemplateId, draft.Fields);
        if (template is null)
            return new(false, "No template matches this draft's fields. Pass a templateId.");

        var stock = config.GetLabel(template.Label);
        if (stock is null)
            return new(false, $"Template '{template.Id}' references unknown label '{template.Label}'.");

        var printer = PrinterForStock(stock);
        var orientation = DefaultOrientation(template);
        var elements = ElementsFor(template, orientation);

        // Gather the SVGs for any pictogram values this draft actually uses.
        var pictograms = new Dictionary<string, string>();
        foreach (var el in elements)
        {
            if (el.Type != "pictogram") continue;
            if (draft.Fields.TryGetValue(el.Field, out var name) && !string.IsNullOrEmpty(name)
                && config.GetPictogram(name)?.Svg is { } svg)
                pictograms[name] = svg;
        }

        byte[] job;
        try
        {
            var http = httpFactory.CreateClient();
            http.Timeout = TimeSpan.FromSeconds(20);
            var resp = await http.PostAsJsonAsync($"{_renderUrl}/render",
                new { fields = draft.Fields, elements, orientation, stock, printer, pictograms }, ct);
            if (!resp.IsSuccessStatusCode)
                return new(false, $"Render service returned {(int)resp.StatusCode}.");
            job = await resp.Content.ReadAsByteArrayAsync(ct);
        }
        catch (Exception ex)
        {
            return new(false, $"Render service unreachable: {ex.Message}");
        }
        if (job.Length == 0)
            return new(false, "Renderer produced an empty job.");

        var target = agentId ?? agents.List().FirstOrDefault(a => a.IsDefault)?.Id;
        if (string.IsNullOrEmpty(target))
            return new(false, "No printer specified and no default printer is set.");

        return await registry.SendAsync(target, job, ct) switch
        {
            PrintResult.Ok => new(true, AgentId: target, Bytes: job.Length),
            PrintResult.PrinterNotReady => new(false, $"Printer '{target}' is connected but has no media/printer ready.", target),
            _ => new(false, $"Printer '{target}' is not connected.", target),
        };
    }

    private LabelTemplate? ResolveTemplate(string? templateId, IDictionary<string, string> fields)
    {
        if (!string.IsNullOrEmpty(templateId))
            return config.GetTemplate(templateId);
        return config.GetTemplates().FirstOrDefault(t =>
            t.RequiredFields.All(f => fields.TryGetValue(f, out var v) && !string.IsNullOrWhiteSpace(v)));
    }

    private Printer PrinterForStock(LabelStock stock)
    {
        foreach (var id in stock.CompatiblePrinters ?? [])
            if (config.GetPrinter(id) is { } p) return p;
        return new Printer { Id = "default", Name = "Default", Dpi = 300 };
    }

    // The elements/orientation selection (pure config logic, shared with the UI).
    private static List<TemplateElement> ElementsFor(LabelTemplate t, string orientation)
    {
        if (t.Variants is { Count: > 0 })
        {
            if (t.Variants.TryGetValue(orientation, out var v) && v.Elements.Count > 0) return v.Elements;
            if (t.Variants.TryGetValue("portrait", out var p)) return p.Elements;
            if (t.Variants.TryGetValue("landscape", out var l)) return l.Elements;
            return new();
        }
        return t.Elements ?? new();
    }

    private static string DefaultOrientation(LabelTemplate t)
    {
        if (t.Variants is { Count: > 0 })
            return t.Variants.ContainsKey("portrait") ? "portrait" : t.Variants.Keys.First();
        return t.Orientation ?? "portrait";
    }
}
