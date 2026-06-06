namespace Lablr.Api.Services;

public sealed record PrintDraftResult(bool Ok, string? Error = null, string? AgentId = null, int Bytes = 0);

/// <summary>
/// Headless print pipeline: resolve a draft to its template/stock/printer,
/// render it server-side (<see cref="LabelRenderer"/>), and relay the bytes to a
/// connected bridge. Used by the AI (MCP print_draft) — no browser in the loop.
/// </summary>
public sealed class LabelPrintService(
    ConfigService config, DraftStore drafts, PrintAgentStore agents,
    PrintAgentRegistry registry, LabelRenderer renderer)
{
    public async Task<PrintDraftResult> PrintDraftAsync(
        string draftId, string? templateId, string? agentId, CancellationToken ct)
    {
        var draft = drafts.Get(draftId);
        if (draft is null)
            return new(false, $"Unknown draft '{draftId}'.");

        // Template: explicit arg > the draft's own template > first auto-match.
        var template = ResolveTemplate(templateId ?? draft.TemplateId, draft.Fields);
        if (template is null)
            return new(false, "No template matches this draft's fields. Pass a templateId.");

        var stock = config.GetLabel(template.Label);
        if (stock is null)
            return new(false, $"Template '{template.Id}' references unknown label '{template.Label}'.");

        var printer = PrinterForStock(stock);
        var orientation = LabelRenderer.DefaultOrientation(template);

        byte[] job;
        try
        {
            job = renderer.RenderJob(template, draft.Fields, stock, printer, orientation);
        }
        catch (Exception ex)
        {
            return new(false, $"Render failed: {ex.Message}");
        }

        // Agent: explicit arg > the starred default.
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

        // First template whose required fields are all present + non-empty.
        return config.GetTemplates().FirstOrDefault(t =>
            t.RequiredFields.All(f => fields.TryGetValue(f, out var v) && !string.IsNullOrWhiteSpace(v)));
    }

    private Printer PrinterForStock(LabelStock stock)
    {
        foreach (var id in stock.CompatiblePrinters ?? [])
            if (config.GetPrinter(id) is { } p) return p;
        return new Printer { Id = "default", Name = "Default", Dpi = 300 };
    }
}
