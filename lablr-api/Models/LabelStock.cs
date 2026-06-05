using System.ComponentModel.DataAnnotations;
using System.ComponentModel.DataAnnotations.Schema;

namespace Lablr.Api.Models;

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

/// <summary>A physical label product (a roll/sheet of a specific size & material).</summary>
[Table("labels")]
public sealed class LabelStock
{
    [Key]
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
