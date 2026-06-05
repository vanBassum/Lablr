using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Metadata.Builders;

namespace Lablr.Api.Data;

public sealed class LabelStockConfiguration : IEntityTypeConfiguration<LabelStock>
{
    public void Configure(EntityTypeBuilder<LabelStock> e)
    {
        e.Property(x => x.MarginsMm).HasJsonConversion();
        e.Property(x => x.OffsetCorrectionMm).HasJsonConversion();
        e.Property(x => x.CompatiblePrinters).HasJsonConversion();
    }
}
