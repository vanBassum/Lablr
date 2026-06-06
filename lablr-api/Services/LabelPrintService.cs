namespace Lablr.Api.Services;

public sealed record PrintDraftResult(bool Ok, string? Error = null, string? AgentId = null, int Bytes = 0);

/// <summary>A draft resolved to everything the renderer needs.</summary>
public sealed record ResolvedDraft(
    LabelTemplate Template, IReadOnlyDictionary<string, string> Fields,
    LabelStock Stock, Printer Printer, string Orientation);

/// <summary>
/// Resolves a draft to its template/stock/printer/orientation and drives the
/// single renderer (<see cref="LabelRenderer"/>) for the preview image, the USB
/// job bytes, and the headless AI print path (render + relay to a bridge). The
/// renderer is the only place a bitmap is produced, so preview == print.
/// </summary>
public sealed class LabelPrintService(
    ConfigService config, DraftStore drafts, PrintAgentStore agents,
    PrintAgentRegistry registry, LabelRenderer renderer)
{
    /// <summary>Resolve a draft (+ optional template/orientation overrides) to render inputs.</summary>
    public (ResolvedDraft? Resolved, string? Error) Resolve(
        string draftId, string? templateId, string? orientation)
    {
        var draft = drafts.Get(draftId);
        if (draft is null) return (null, $"Unknown draft '{draftId}'.");

        // Template: explicit arg > the draft's own template > first auto-match.
        var template = ResolveTemplate(templateId ?? draft.TemplateId, draft.Fields);
        if (template is null) return (null, "No template matches this draft's fields. Pass a templateId.");

        var stock = config.GetLabel(template.Label);
        if (stock is null) return (null, $"Template '{template.Id}' references unknown label '{template.Label}'.");

        var printer = PrinterForStock(stock);
        var o = !string.IsNullOrWhiteSpace(orientation) ? orientation! : LabelRenderer.DefaultOrientation(template);
        return (new ResolvedDraft(template, draft.Fields, stock, printer, o), null);
    }

    /// <summary>Render the preview PNG for a draft (what the frontend displays).</summary>
    public (byte[]? Png, string? Error) RenderPreview(string draftId, string? templateId, string? orientation)
    {
        var (r, err) = Resolve(draftId, templateId, orientation);
        if (r is null) return (null, err);
        return (renderer.RenderPreviewPng(r.Template, r.Fields, r.Stock, r.Printer, r.Orientation), null);
    }

    /// <summary>
    /// Preview a template with sample values (field name as text; the first
    /// pictogram for pictogram slots) — for the Templates admin page, no draft needed.
    /// </summary>
    public (byte[]? Png, string? Error) RenderTemplatePreview(string templateId, string? orientation)
    {
        var template = config.GetTemplate(templateId);
        if (template is null) return (null, $"Unknown template '{templateId}'.");
        var stock = config.GetLabel(template.Label);
        if (stock is null) return (null, $"Template '{template.Id}' references unknown label '{template.Label}'.");

        var printer = PrinterForStock(stock);
        var o = !string.IsNullOrWhiteSpace(orientation) ? orientation! : LabelRenderer.DefaultOrientation(template);
        var firstPic = config.GetPictograms().FirstOrDefault()?.Name ?? "";
        var fields = new Dictionary<string, string>();
        foreach (var el in LabelRenderer.ElementsFor(template, o))
            fields[el.Field] = el.Type == "pictogram" ? firstPic : el.Field;

        return (renderer.RenderPreviewPng(template, fields, stock, printer, o), null);
    }

    /// <summary>Render the printer job bytes for a draft (for desktop WebUSB).</summary>
    public (byte[]? Job, string? Error) RenderJob(
        string draftId, string? templateId, string? orientation, int nudgeX, int nudgeY)
    {
        var (r, err) = Resolve(draftId, templateId, orientation);
        if (r is null) return (null, err);
        return (renderer.RenderJob(r.Template, r.Fields, r.Stock, r.Printer, r.Orientation, nudgeX, nudgeY), null);
    }

    /// <summary>Headless print: render the job and relay it to a connected bridge.</summary>
    public async Task<PrintDraftResult> PrintDraftAsync(
        string draftId, string? templateId, string? agentId,
        string? orientation, int nudgeX, int nudgeY, CancellationToken ct)
    {
        var (r, err) = Resolve(draftId, templateId, orientation);
        if (r is null) return new(false, err);

        byte[] job;
        try { job = renderer.RenderJob(r.Template, r.Fields, r.Stock, r.Printer, r.Orientation, nudgeX, nudgeY); }
        catch (Exception ex) { return new(false, $"Render failed: {ex.Message}"); }

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
