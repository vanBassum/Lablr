namespace Lablr.Api;

// Config model — mirrors the YAML schema in /label-config and the TS types in
// lablr-ui/src/types.ts. Plain mutable classes so YamlDotNet can populate them.

public sealed class Margins
{
    public double Top { get; set; }
    public double Right { get; set; }
    public double Bottom { get; set; }
    public double Left { get; set; }
}

public sealed class Offset
{
    public double X { get; set; }
    public double Y { get; set; }
}

public sealed class LabelStock
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public double WidthMm { get; set; }
    public double HeightMm { get; set; }
    public string? Material { get; set; }
    public Margins? MarginsMm { get; set; }
    public Offset? OffsetCorrectionMm { get; set; }
    public List<string>? CompatiblePrinters { get; set; }
    public string? Manufacturer { get; set; }
    public string? Sku { get; set; }
}

public sealed class Rect
{
    public double X { get; set; }
    public double Y { get; set; }
    public double Width { get; set; }
    public double Height { get; set; }
}

public sealed class Font
{
    public double? MaxSizeMm { get; set; }
    public double? MinSizeMm { get; set; }
    public string? Weight { get; set; }
}

public sealed class TemplateElement
{
    public string Type { get; set; } = "text";
    public string Field { get; set; } = "";
    public Rect Rect { get; set; } = new();
    public string? Align { get; set; }
    public string? Valign { get; set; }
    public bool? Wrap { get; set; }
    public int? MaxLines { get; set; }
    public string? Fit { get; set; }
    public Font? Font { get; set; }
}

public sealed class TemplateVariant
{
    public List<TemplateElement> Elements { get; set; } = new();
}

public sealed class LabelTemplate
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Label { get; set; } = "";
    public List<string> RequiredFields { get; set; } = new();
    public List<string>? OptionalFields { get; set; }
    public string? Orientation { get; set; }
    public List<TemplateElement>? Elements { get; set; }
    public Dictionary<string, TemplateVariant>? Variants { get; set; }
}

public sealed class Printer
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public int Dpi { get; set; }
}

public sealed class Pictogram
{
    public string Image { get; set; } = "";
}

/// <summary>The whole config as served at GET /api/config.</summary>
public sealed class LabelConfig
{
    public List<LabelStock> Labels { get; set; } = new();
    public List<LabelTemplate> Templates { get; set; } = new();
    public List<Printer> Printers { get; set; } = new();
    public Dictionary<string, Pictogram> Pictograms { get; set; } = new();
}

// --- Drafts ---

/// <summary>A draft as returned by the API (data only).</summary>
public sealed record DraftDto(string Id, Dictionary<string, string> Fields, string? TemplateId);

/// <summary>POST /api/drafts body.</summary>
public sealed class CreateDraftRequest
{
    public string? TemplateId { get; set; }
    public Dictionary<string, string> Fields { get; set; } = new();
}

// --- YAML file wrappers ---

internal sealed class PictogramFile
{
    public Dictionary<string, Pictogram> Pictograms { get; set; } = new();
}

internal sealed class DraftFile
{
    public Dictionary<string, string> Fields { get; set; } = new();
}
