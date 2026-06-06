using Microsoft.EntityFrameworkCore;

namespace Lablr.Api.Data;

/// <summary>
/// SQLite-backed config store. Tables/keys are declared with attributes on the
/// entities; the deeply-nested members (elements, variants, margins…) are mapped
/// to JSON text columns by per-entity IEntityTypeConfiguration classes.
/// </summary>
public sealed class LablrDbContext(DbContextOptions<LablrDbContext> options) : DbContext(options)
{
    public DbSet<LabelStock> Labels => Set<LabelStock>();
    public DbSet<LabelTemplate> Templates => Set<LabelTemplate>();
    public DbSet<Printer> Printers => Set<Printer>();
    public DbSet<Pictogram> Pictograms => Set<Pictogram>();
    public DbSet<PrintAgent> PrintAgents => Set<PrintAgent>();

    protected override void OnModelCreating(ModelBuilder modelBuilder) =>
        modelBuilder.ApplyConfigurationsFromAssembly(typeof(LablrDbContext).Assembly);
}
