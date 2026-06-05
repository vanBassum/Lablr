using System.ComponentModel;
using ModelContextProtocol;
using ModelContextProtocol.Server;

namespace Lablr.Api;

/// <summary>
/// MCP tools an AI uses to make a label: discover templates, then create a draft
/// and get a link the user opens on their phone to preview and print. The server
/// never renders — it only stores draft data and returns a deep link.
/// </summary>
[McpServerToolType]
public sealed class LabelTools
{
    [McpServerTool(Name = "list_templates")]
    [Description("List the available label templates and the fields each one needs. " +
                 "Pick the template that matches what the user is labeling, then call create_draft.")]
    public static object ListTemplates(ConfigService config) =>
        config.Config.Templates.Select(t => new
        {
            id = t.Id,
            name = t.Name,
            requiredFields = t.RequiredFields,
            optionalFields = t.OptionalFields ?? [],
        });

    [McpServerTool(Name = "list_pictograms")]
    [Description("List the available pictogram names (e.g. flammable, corrosive, oxidizing). " +
                 "Use these as the values for a template's pictogram1..N fields.")]
    public static object ListPictograms(ConfigService config) =>
        config.Config.Pictograms.Keys.OrderBy(k => k, StringComparer.Ordinal);

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
        var template = config.Config.Templates.FirstOrDefault(t => t.Id == templateId)
            ?? throw new McpException($"Unknown template '{templateId}'. Call list_templates first.");

        var missing = template.RequiredFields
            .Where(f => !fields.TryGetValue(f, out var v) || string.IsNullOrWhiteSpace(v))
            .ToList();
        if (missing.Count > 0)
            throw new McpException($"Missing required fields for '{templateId}': {string.Join(", ", missing)}");

        var draft = store.Create(fields, templateId);
        return new { id = draft.Id, url = links.DraftUrl(draft.Id) };
    }
}
