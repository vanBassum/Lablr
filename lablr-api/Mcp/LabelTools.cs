using System.ComponentModel;
using ModelContextProtocol;
using ModelContextProtocol.Server;

namespace Lablr.Api.Mcp;

/// <summary>
/// MCP tools an AI uses to make labels — and to author the config itself.
/// These are thin adapters over <see cref="ConfigService"/> / <see cref="DraftStore"/>:
/// all rules + validation live in the service, shared with the REST controllers.
/// The server never renders — it only stores data and returns links.
/// </summary>
[McpServerToolType]
public sealed class LabelTools
{
    // ---------- Reads ----------

    [McpServerTool(Name = "list_templates")]
    [Description("List the label templates and the fields each one needs. " +
                 "Pick the template that matches what the user is labeling, then call create_draft.")]
    public static object ListTemplates(ConfigService config) =>
        config.GetTemplates().Select(t => new
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
        config.GetLabels().Select(l => new
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
        config.GetPictograms().Select(p => p.Name).OrderBy(n => n, StringComparer.Ordinal);

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
        config.ValidateDraft(templateId, fields);
        var draft = store.Create(fields, templateId);
        return new { id = draft.Id, url = links.DraftUrl(draft.Id) };
    }

    // ---------- Config authoring (validation lives in ConfigService) ----------

    [McpServerTool(Name = "upsert_label")]
    [Description("Create or replace a label stock (a physical roll), by id. Sizes are in millimetres.")]
    public static object UpsertLabel(ConfigService config, [Description("The full label stock.")] LabelStock label)
    {
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
        var p = config.UpsertPictogram(name, svg);
        return new { name = p.Name, image = p.Image, ok = true };
    }

    [McpServerTool(Name = "delete_pictogram")]
    [Description("Delete a pictogram by name.")]
    public static object DeletePictogram(ConfigService config, string name) =>
        new { name, deleted = config.DeletePictogram(name) };

    [McpServerTool(Name = "upsert_printer")]
    [Description("Create or replace a printer profile (id, name, dpi).")]
    public static object UpsertPrinter(ConfigService config, [Description("The printer profile.")] Printer printer)
    {
        config.UpsertPrinter(printer);
        return new { id = printer.Id, ok = true };
    }

    [McpServerTool(Name = "delete_printer")]
    [Description("Delete a printer profile by id.")]
    public static object DeletePrinter(ConfigService config, string id) =>
        new { id, deleted = config.DeletePrinter(id) };
}
