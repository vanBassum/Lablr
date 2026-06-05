using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Metadata.Builders;

namespace Lablr.Api.Data;

public sealed class LabelTemplateConfiguration : IEntityTypeConfiguration<LabelTemplate>
{
    public void Configure(EntityTypeBuilder<LabelTemplate> e)
    {
        e.Property(x => x.RequiredFields).HasJsonConversion();
        e.Property(x => x.OptionalFields).HasJsonConversion();
        e.Property(x => x.Elements).HasJsonConversion();
        e.Property(x => x.Variants).HasJsonConversion();
    }
}
