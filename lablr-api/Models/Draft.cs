namespace Lablr.Api.Models;

/// <summary>A draft as returned by the API (data only).</summary>
public sealed record DraftDto(string Id, Dictionary<string, string> Fields, string? TemplateId);

/// <summary>POST /api/drafts body.</summary>
public sealed class CreateDraftRequest
{
    public string? TemplateId { get; set; }
    public Dictionary<string, string> Fields { get; set; } = new();
}
