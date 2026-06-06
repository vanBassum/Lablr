using System.Security.Cryptography;
using Microsoft.EntityFrameworkCore;

namespace Lablr.Api.Services;

/// <summary>
/// Persistent registry of print bridges (name + secret token), in SQLite.
/// Live connection state lives in <see cref="PrintAgentRegistry"/>; this is just
/// the durable "which printers exist and what's their token" record.
/// </summary>
public sealed class PrintAgentStore
{
    private readonly IDbContextFactory<LablrDbContext> _dbf;

    public PrintAgentStore(IDbContextFactory<LablrDbContext> dbf)
    {
        _dbf = dbf;
        using var db = _dbf.CreateDbContext();
        // ConfigService uses EnsureCreated(), which does NOT add a new table to an
        // already-existing DB. Create ours explicitly so it appears on upgrades too.
        db.Database.ExecuteSqlRaw(
            "CREATE TABLE IF NOT EXISTS print_agents (" +
            "Id TEXT NOT NULL CONSTRAINT PK_print_agents PRIMARY KEY, " +
            "Name TEXT NOT NULL, Token TEXT NOT NULL, CreatedAt TEXT NOT NULL)");
    }

    public List<PrintAgent> List()
    {
        using var db = _dbf.CreateDbContext();
        return db.PrintAgents.AsNoTracking()
            .OrderBy(a => a.Name).ToList();
    }

    public PrintAgent? Find(string id)
    {
        using var db = _dbf.CreateDbContext();
        return db.PrintAgents.AsNoTracking().FirstOrDefault(a => a.Id == id);
    }

    public PrintAgent? FindByToken(string token)
    {
        if (string.IsNullOrEmpty(token)) return null;
        using var db = _dbf.CreateDbContext();
        return db.PrintAgents.AsNoTracking().FirstOrDefault(a => a.Token == token);
    }

    public PrintAgent Create(string name)
    {
        var agent = new PrintAgent
        {
            Id = "ag_" + Rand(6),
            Name = string.IsNullOrWhiteSpace(name) ? "Printer" : name.Trim(),
            Token = Rand(24),
            CreatedAt = DateTimeOffset.UtcNow,
        };
        using var db = _dbf.CreateDbContext();
        db.PrintAgents.Add(agent);
        db.SaveChanges();
        return agent;
    }

    public PrintAgent? Rename(string id, string name)
    {
        using var db = _dbf.CreateDbContext();
        var a = db.PrintAgents.Find(id);
        if (a is null) return null;
        if (!string.IsNullOrWhiteSpace(name)) a.Name = name.Trim();
        db.SaveChanges();
        return a;
    }

    public bool Delete(string id)
    {
        using var db = _dbf.CreateDbContext();
        var a = db.PrintAgents.Find(id);
        if (a is null) return false;
        db.PrintAgents.Remove(a);
        db.SaveChanges();
        return true;
    }

    private static string Rand(int bytes)
    {
        Span<byte> b = stackalloc byte[bytes];
        RandomNumberGenerator.Fill(b);
        return Convert.ToHexString(b).ToLowerInvariant();
    }
}
