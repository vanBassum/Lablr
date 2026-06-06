using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Design;

namespace Lablr.Api.Data;

/// <summary>
/// Design-time factory used only by <c>dotnet ef</c> (migrations). It builds the
/// context directly so scaffolding doesn't boot the app (and run startup seeding).
/// The connection string is irrelevant for `migrations add` — it isn't opened.
/// </summary>
public sealed class LablrDbContextFactory : IDesignTimeDbContextFactory<LablrDbContext>
{
    public LablrDbContext CreateDbContext(string[] args)
    {
        var options = new DbContextOptionsBuilder<LablrDbContext>()
            .UseSqlite("Data Source=design-time.db")
            .Options;
        return new LablrDbContext(options);
    }
}
