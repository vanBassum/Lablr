using System.Text.Json;
using Microsoft.EntityFrameworkCore.ChangeTracking;
using Microsoft.EntityFrameworkCore.Metadata.Builders;
using Microsoft.EntityFrameworkCore.Storage.ValueConversion;

namespace Lablr.Api.Data;

internal static class JsonConversionExtensions
{
    internal static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    /// <summary>Persist a property as a JSON text column (with a value comparer so
    /// EF change-tracks it correctly).</summary>
    public static PropertyBuilder<T> HasJsonConversion<T>(this PropertyBuilder<T> builder)
    {
        var opts = JsonOptions;
        var converter = new ValueConverter<T, string>(
            v => JsonSerializer.Serialize(v, opts),
            s => JsonSerializer.Deserialize<T>(s, opts)!);
        var comparer = new ValueComparer<T>(
            (a, c) => JsonSerializer.Serialize(a, opts) == JsonSerializer.Serialize(c, opts),
            v => v == null ? 0 : JsonSerializer.Serialize(v, opts).GetHashCode(),
            v => JsonSerializer.Deserialize<T>(JsonSerializer.Serialize(v, opts), opts)!);
        builder.HasConversion(converter, comparer);
        return builder;
    }
}
