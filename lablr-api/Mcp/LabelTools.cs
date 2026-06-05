using System.ComponentModel;
using System.Text.RegularExpressions;
using ModelContextProtocol;
using ModelContextProtocol.Server;

namespace Lablr.Api.Mcp;

/// <summary>
/// MCP tools an AI uses to make labels — and to author the config itself.
/// Reads: discover templates/labels/pictograms. Writes: create/update/delete
/// templates, labels, pictograms and printers (stored in SQLite). create_draft
/// turns data into a deep link the user opens to preview and print. The server
/// never renders — it only stores data and returns links.
/// </summary>
[McpServerToolType]
public sealed class LabelTools
{
    // ---------- Reads ----------

    [McpServerTool(Name = "list_templates")]
    [Description("List the label templates and the fields each one needs. " +
                 "Pick the template that matches what the user is labeling, then call create_draft.")]
    public static object ListTemplates(ConfigService config) =>
        config.GetConfig().Templates.Select(t => new
        {
            id = t.Id,
            name = t.Name,
            label = t.Label,
            requiredFields = t.RequiredFields,
            optionalFields = t.OptionalFields ?? [],
        });

    [McpServerTool(Name = "get_template")]
    [Description("Get one template in full (elements/variants and all) — useful before editing it.")]
    public static object GetTemplate(ConfigService config, string id) =>
        config.GetTemplate(id) ?? throw new McpException($"Unknown template '{id}'.");

    [McpServerTool(Name = "list_labels")]
    [Description("List the label stocks (physical rolls) a template can target.")]
    public static object ListLabels(ConfigService config) =>
        config.GetConfig().Labels.Select(l => new
        {
            id = l.Id,
            name = l.Name,
            widthMm = l.WidthMm,
            heightMm = l.HeightMm,
            compatiblePrinters = l.CompatiblePrinters ?? [],
        });

    [McpServerTool(Name = "list_pictograms")]
    [Description("List the available pictogram names (e.g. flammable, corrosive, oxidizing). " +
                 "Use these as the values for a template's pictogram1..N fields.")]
    public static object ListPictograms(ConfigService config) =>
        config.GetConfig().Pictograms.Keys.OrderBy(k => k, StringComparer.Ordinal);

    // ---------- Drafts ----------

    [McpServerTool(Name = "create_draft")]
    [Description("Create a label draft from a template and field values, and return a URL " +
                 "the user opens on their phone to preview and print. Fill every required field.")]
    public static object CreateDraft(
        ConfigService config,
        DraftStore store,
        LinkService links,
        [Description("Template id from list_templates.")] string templateId,
        [Description("Field values keyed by field name (use the template's field keys).")]
        Dictionary<string, string> fields)
    {
        var template = config.GetTemplate(templateId)
            ?? throw new McpException($"Unknown template '{templateId}'. Call list_templates first.");

        var missing = template.RequiredFields
            .Where(f => !fields.TryGetValue(f, out var v) || string.IsNullOrWhiteSpace(v))
            .ToList();
        if (missing.Count > 0)
            throw new McpException($"Missing required fields for '{templateId}': {string.Join(", ", missing)}");

        var draft = store.Create(fields, templateId);
        return new { id = draft.Id, url = links.DraftUrl(draft.Id) };
    }

    // ---------- Config authoring ----------

    [McpServerTool(Name = "upsert_label")]
    [Description("Create or replace a label stock (a physical roll), by id. Sizes are in millimetres.")]
    public static object UpsertLabel(ConfigService config, [Description("The full label stock.")] LabelStock label)
    {
        if (string.IsNullOrWhiteSpace(label.Id)) throw new McpException("label.id is required.");
        if (string.IsNullOrWhiteSpace(label.Name)) throw new McpException("label.name is required.");
        if (label.WidthMm <= 0 || label.HeightMm <= 0)
            throw new McpException("label.widthMm and label.heightMm must be greater than 0.");
        config.UpsertLabel(label);
        return new { id = label.Id, ok = true };
    }

    [McpServerTool(Name = "delete_label")]
    [Description("Delete a label stock by id.")]
    public static object DeleteLabel(ConfigService config, string id) =>
        new { id, deleted = config.DeleteLabel(id) };

    [McpServerTool(Name = "upsert_template")]
    [Description("Create or replace a template, by id. The label must already exist (list_labels / " +
                 "upsert_label). Provide either `elements` (single layout) or `variants` " +
                 "(portrait/landscape). Coordinates are absolute millimetres.")]
    public static object UpsertTemplate(ConfigService config, [Description("The full template.")] LabelTemplate template)
    {
        if (string.IsNullOrWhiteSpace(template.Id)) throw new McpException("template.id is required.");
        if (string.IsNullOrWhiteSpace(template.Name)) throw new McpException("template.name is required.");

        var cfg = config.GetConfig();
        if (!cfg.Labels.Any(l => l.Id == template.Label))
            throw new McpException(
                $"Unknown label '{template.Label}'. Call list_labels, or create it with upsert_label first.");

        var hasElements = template.Elements is { Count: > 0 };
        var hasVariants = template.Variants is { Count: > 0 };
        if (!hasElements && !hasVariants)
            throw new McpException("Provide either `elements` or `variants`.");

        config.UpsertTemplate(template);
        return new { id = template.Id, ok = true };
    }

    [McpServerTool(Name = "delete_template")]
    [Description("Delete a template by id.")]
    public static object DeleteTemplate(ConfigService config, string id) =>
        new { id, deleted = config.DeleteTemplate(id) };

    [McpServerTool(Name = "upsert_pictogram")]
    [Description("Create or replace a pictogram by name, with inline SVG markup. Reference it from a " +
                 "template's pictogramN fields by this name. Keep SVGs small and monochrome-friendly.")]
    public static object UpsertPictogram(
        ConfigService config,
        [Description("Symbol name, e.g. 'flammable'.")] string name,
        [Description("Raw SVG markup (must contain an <svg> element).")] string svg)
    {
        if (string.IsNullOrWhiteSpace(name)) throw new McpException("name is required.");
        if (string.IsNullOrWhiteSpace(svg) || !svg.Contains("<svg"))
            throw new McpException("svg must be SVG markup containing an <svg> element.");

        // Keep an existing pictogram's filename so its URL is stable; otherwise derive one.
        var existing = config.GetConfig().Pictograms.GetValueOrDefault(name)?.Image;
        var image = existing ?? Slug(name) + ".svg";
        config.UpsertPictogram(new Pictogram { Name = name, Image = image, Svg = svg });
        return new { name, image, ok = true };
    }

    [McpServerTool(Name = "delete_pictogram")]
    [Description("Delete a pictogram by name.")]
    public static object DeletePictogram(ConfigService config, string name) =>
        new { name, deleted = config.DeletePictogram(name) };

    [McpServerTool(Name = "upsert_printer")]
    [Description("Create or replace a printer profile (id, name, dpi).")]
    public static object UpsertPrinter(ConfigService config, [Description("The printer profile.")] Printer printer)
    {
        if (string.IsNullOrWhiteSpace(printer.Id)) throw new McpException("printer.id is required.");
        if (printer.Dpi <= 0) throw new McpException("printer.dpi must be greater than 0.");
        config.UpsertPrinter(printer);
        return new { id = printer.Id, ok = true };
    }

    [McpServerTool(Name = "delete_printer")]
    [Description("Delete a printer profile by id.")]
    public static object DeletePrinter(ConfigService config, string id) =>
        new { id, deleted = config.DeletePrinter(id) };

    private static string Slug(string s) =>
        Regex.Replace(s.ToLowerInvariant(), "[^a-z0-9]+", "-").Trim('-');
}
