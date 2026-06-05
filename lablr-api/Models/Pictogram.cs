using System.ComponentModel.DataAnnotations;
using System.ComponentModel.DataAnnotations.Schema;

namespace Lablr.Api.Models;

/// <summary>Stored pictogram: a named symbol with its inline SVG (EF entity).</summary>
[Table("pictograms")]
public sealed class Pictogram
{
    [Key]
    public string Name { get; set; } = "";
    public string Image { get; set; } = ""; // filename the UI requests at /pictograms/{image}
    public string Svg { get; set; } = "";
}

/// <summary>Pictogram reference as served in /api/config (name → { image }).</summary>
public sealed class PictogramRef
{
    public string Image { get; set; } = "";
}
