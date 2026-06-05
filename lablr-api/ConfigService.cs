using YamlDotNet.Serialization;
using YamlDotNet.Serialization.NamingConventions;

namespace Lablr.Api;

/// <summary>
/// Loads the label config (labels, templates, printers, pictograms, seed drafts)
/// from a directory on disk. The directory is a mount point in production
/// (Config:Dir), so config is edited without rebuilding the image. A best-effort
/// file watcher reloads on change; a container restart always reloads.
/// </summary>
public sealed class ConfigService : IDisposable
{
    private readonly string _dir;
    private readonly ILogger<ConfigService> _log;
    private readonly IDeserializer _yaml = new DeserializerBuilder()
        .WithNamingConvention(CamelCaseNamingConvention.Instance)
        .IgnoreUnmatchedProperties()
        .Build();

    private volatile LabelConfig _config = new();
    private FileSystemWatcher? _watcher;
    private readonly Lock _reloadLock = new();

    public ConfigService(IConfiguration cfg, IHostEnvironment env, ILogger<ConfigService> log)
    {
        _log = log;
        var dir = cfg["Config:Dir"] ?? "label-config";
        _dir = Path.IsPathRooted(dir) ? dir : Path.GetFullPath(dir, env.ContentRootPath);
        Reload();
        StartWatching();
    }

    public LabelConfig Config => _config;
    public string PictogramsDir => Path.Combine(_dir, "pictograms");

    public void Reload()
    {
        lock (_reloadLock)
        {
            var c = new LabelConfig
            {
                Labels = LoadDir<LabelStock>("labels"),
                Templates = LoadDir<LabelTemplate>("templates"),
                Printers = LoadDir<Printer>("printers"),
            };

            var pictoFile = Path.Combine(_dir, "pictograms.yaml");
            if (File.Exists(pictoFile))
            {
                try
                {
                    c.Pictograms = _yaml.Deserialize<PictogramFile>(File.ReadAllText(pictoFile))?.Pictograms
                                   ?? new();
                }
                catch (Exception e)
                {
                    _log.LogError(e, "Failed to parse {File}", pictoFile);
                }
            }

            _config = c;
            _log.LogInformation(
                "Loaded config from {Dir}: {L} labels, {T} templates, {P} printers, {G} pictograms",
                _dir, c.Labels.Count, c.Templates.Count, c.Printers.Count, c.Pictograms.Count);
        }
    }

    /// <summary>Seed drafts shipped with the config (id = filename); data only.</summary>
    public IEnumerable<(string Id, Dictionary<string, string> Fields)> LoadSeedDrafts()
    {
        var path = Path.Combine(_dir, "drafts");
        if (!Directory.Exists(path)) yield break;
        foreach (var file in Directory.EnumerateFiles(path, "*.yaml").OrderBy(x => x))
        {
            Dictionary<string, string>? fields = null;
            try
            {
                fields = _yaml.Deserialize<DraftFile>(File.ReadAllText(file))?.Fields;
            }
            catch (Exception e)
            {
                _log.LogError(e, "Failed to parse draft {File}", file);
            }
            if (fields is { Count: > 0 })
                yield return (Path.GetFileNameWithoutExtension(file), fields);
        }
    }

    private List<T> LoadDir<T>(string subdir)
    {
        var path = Path.Combine(_dir, subdir);
        var list = new List<T>();
        if (!Directory.Exists(path))
        {
            _log.LogWarning("Config subdirectory missing: {Path}", path);
            return list;
        }
        foreach (var file in Directory.EnumerateFiles(path, "*.yaml").OrderBy(x => x))
        {
            try
            {
                var item = _yaml.Deserialize<T>(File.ReadAllText(file));
                if (item != null) list.Add(item);
            }
            catch (Exception e)
            {
                _log.LogError(e, "Failed to parse {File}", file);
            }
        }
        return list;
    }

    private void StartWatching()
    {
        if (!Directory.Exists(_dir)) return;
        try
        {
            _watcher = new FileSystemWatcher(_dir)
            {
                IncludeSubdirectories = true,
                NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.Size,
                EnableRaisingEvents = true,
            };
            FileSystemEventHandler reload = (_, _) =>
            {
                // Debounce bursts of events; reload is cheap and idempotent.
                try { Reload(); } catch (Exception e) { _log.LogError(e, "Config reload failed"); }
            };
            _watcher.Changed += reload;
            _watcher.Created += reload;
            _watcher.Deleted += reload;
            _watcher.Renamed += (_, _) => reload(this, null!);
        }
        catch (Exception e)
        {
            // Watching can fail on some mounts; startup load still applies, restart reloads.
            _log.LogWarning(e, "Config file watcher unavailable; reload on restart only");
        }
    }

    public void Dispose() => _watcher?.Dispose();
}
