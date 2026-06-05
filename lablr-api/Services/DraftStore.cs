using System.Collections.Concurrent;
using System.Security.Cryptography;

namespace Lablr.Api.Services;

/// <summary>
/// Ephemeral, in-memory draft storage. Seed drafts (shipped with the config) are
/// permanent; AI/created drafts expire after the configured TTL. No database.
/// </summary>
public sealed class DraftStore
{
    private sealed record Entry(
        Dictionary<string, string> Fields,
        string? TemplateId,
        DateTimeOffset CreatedAt,
        bool Seed);

    private readonly ConcurrentDictionary<string, Entry> _drafts = new();
    public TimeSpan Ttl { get; }

    public DraftStore(IConfiguration cfg, ConfigService config, ILogger<DraftStore> log)
    {
        Ttl = TimeSpan.FromHours(cfg.GetValue("Drafts:TtlHours", 24.0));
        foreach (var (id, fields) in config.LoadSeedDrafts())
            _drafts[id] = new Entry(fields, null, DateTimeOffset.UtcNow, Seed: true);
        log.LogInformation("Seeded {N} drafts; TTL {Ttl}", _drafts.Count, Ttl);
    }

    public DraftDto? Get(string id) =>
        _drafts.TryGetValue(id, out var e) ? new DraftDto(id, e.Fields, e.TemplateId) : null;

    public IEnumerable<DraftDto> List() =>
        _drafts.Select(kv => new DraftDto(kv.Key, kv.Value.Fields, kv.Value.TemplateId));

    public DraftDto Create(Dictionary<string, string> fields, string? templateId)
    {
        var id = NewId();
        _drafts[id] = new Entry(fields, templateId, DateTimeOffset.UtcNow, Seed: false);
        return new DraftDto(id, fields, templateId);
    }

    /// <summary>Drop expired non-seed drafts. Returns the number removed.</summary>
    public int Sweep()
    {
        var cutoff = DateTimeOffset.UtcNow - Ttl;
        var removed = 0;
        foreach (var kv in _drafts)
        {
            if (!kv.Value.Seed && kv.Value.CreatedAt < cutoff && _drafts.TryRemove(kv.Key, out _))
                removed++;
        }
        return removed;
    }

    // URL-safe, short, collision-resistant enough for ephemeral drafts.
    private static string NewId()
    {
        Span<byte> bytes = stackalloc byte[8];
        RandomNumberGenerator.Fill(bytes);
        return Convert.ToHexString(bytes).ToLowerInvariant();
    }
}

/// <summary>Periodically evicts expired drafts.</summary>
public sealed class DraftSweeper(DraftStore store, ILogger<DraftSweeper> log) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromMinutes(15));
        try
        {
            while (await timer.WaitForNextTickAsync(stoppingToken))
            {
                var n = store.Sweep();
                if (n > 0) log.LogInformation("Swept {N} expired drafts", n);
            }
        }
        catch (OperationCanceledException) { /* shutting down */ }
    }
}
