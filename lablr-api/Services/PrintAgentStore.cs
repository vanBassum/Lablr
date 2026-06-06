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
        // The schema (print_agents incl. IsDefault) is owned by EF migrations,
        // applied at startup by ConfigService before any request is served.
        _dbf = dbf;
    }

    /// <summary>Mark one agent as the shared default (clears the flag on the rest).</summary>
    public bool SetDefault(string id)
    {
        using var db = _dbf.CreateDbContext();
        var target = db.PrintAgents.Find(id);
        if (target is null) return false;
        foreach (var a in db.PrintAgents.Where(a => a.IsDefault))
            a.IsDefault = false;
        target.IsDefault = true;
        db.SaveChanges();
        return true;
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
