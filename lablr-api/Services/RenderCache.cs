using System.Collections.Concurrent;

namespace Lablr.Api.Services;

/// <summary>
/// In-memory cache of rendered bytes (preview PNGs, etc.), keyed by content. Drafts
/// are immutable and config changes bump <see cref="ConfigService.ConfigVersion"/>,
/// so the whole cache is dropped whenever the version changes — entries can never
/// go stale. This makes repeat/concurrent previews (e.g. the draft grid) instant.
/// </summary>
public sealed class RenderCache
{
    private readonly ConcurrentDictionary<string, byte[]> _map = new();
    private long _version = long.MinValue;

    public byte[]? TryGet(string key, long version)
    {
        EnsureVersion(version);
        return _map.TryGetValue(key, out var v) ? v : null;
    }

    public void Set(string key, long version, byte[] value)
    {
        EnsureVersion(version);
        _map[key] = value;
    }

    private void EnsureVersion(long version)
    {
        if (version == Interlocked.Read(ref _version)) return;
        _map.Clear();
        Interlocked.Exchange(ref _version, version);
    }
}
