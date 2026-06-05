using System.ComponentModel.DataAnnotations;
using System.ComponentModel.DataAnnotations.Schema;

namespace Lablr.Api.Models;

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

/// <summary>
/// A handcrafted design for one label stock. Either a single set of `elements`
/// (with an `orientation`), or per-orientation `variants`.
/// </summary>
[Table("templates")]
public sealed class LabelTemplate
{
    [Key]
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Label { get; set; } = ""; // references a LabelStock id
    public List<string> RequiredFields { get; set; } = new();
    public List<string>? OptionalFields { get; set; }
    public string? Orientation { get; set; }
    public List<TemplateElement>? Elements { get; set; }
    public Dictionary<string, TemplateVariant>? Variants { get; set; }
}
