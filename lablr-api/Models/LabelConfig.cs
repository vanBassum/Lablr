namespace Lablr.Api.Models;

/// <summary>The whole config as served at GET /api/config.</summary>
public sealed class LabelConfig
{
    public List<LabelStock> Labels { get; set; } = new();
    public List<LabelTemplate> Templates { get; set; } = new();
    public List<Printer> Printers { get; set; } = new();
    public Dictionary<string, PictogramRef> Pictograms { get; set; } = new();
}
