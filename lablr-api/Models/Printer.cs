using System.ComponentModel.DataAnnotations;
using System.ComponentModel.DataAnnotations.Schema;

namespace Lablr.Api.Models;

/// <summary>Output device: identity + native resolution (sizes the bitmap).</summary>
[Table("printers")]
public sealed class Printer
{
    [Key]
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public int Dpi { get; set; }
}
