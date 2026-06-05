using Microsoft.EntityFrameworkCore;
using YamlDotNet.Serialization;
using YamlDotNet.Serialization.NamingConventions;

namespace Lablr.Api.Services;

/// <summary>
/// The config store. Models (labels, templates, printers, pictograms) live in
/// SQLite via EF Core and are edited at runtime (REST/MCP). On first boot, an
/// empty DB is seeded from the YAML in Config:Dir so existing config carries
/// over; after that the DB is the source of truth. Drafts are seeded into the
/// RAM store from the same YAML on every boot (they're ephemeral, not in the DB).
/// </summary>
public sealed class ConfigService
{
    private readonly IDbContextFactory<LablrDbContext> _dbf;
    private readonly string _seedDir;
    private readonly ILogger<ConfigService> _log;
    private readonly IDeserializer _yaml = new DeserializerBuilder()
        .WithNamingConvention(CamelCaseNamingConvention.Instance)
        .IgnoreUnmatchedProperties()
        .Build();

    public ConfigService(
        IDbContextFactory<LablrDbContext> dbf,
        IConfiguration cfg,
        IHostEnvironment env,
        ILogger<ConfigService> log)
    {
        _dbf = dbf;
        _log = log;
        var dir = cfg["Config:Dir"] ?? "label-config";
        _seedDir = Path.IsPathRooted(dir) ? dir : Path.GetFullPath(dir, env.ContentRootPath);

        using var db = _dbf.CreateDbContext();
        db.Database.EnsureCreated();
        SeedIfEmpty(db);
    }

    // ---------- Reads ----------

    public LabelConfig GetConfig()
    {
        using var db = _dbf.CreateDbContext();
        return new LabelConfig
        {
            Labels = db.Labels.AsNoTracking().ToList(),
            Templates = db.Templates.AsNoTracking().ToList(),
            Printers = db.Printers.AsNoTracking().ToList(),
            Pictograms = db.Pictograms.AsNoTracking().ToDictionary(
                p => p.Name, p => new PictogramRef { Image = p.Image }),
        };
    }

    public LabelTemplate? GetTemplate(string id)
    {
        using var db = _dbf.CreateDbContext();
        return db.Templates.AsNoTracking().FirstOrDefault(t => t.Id == id);
    }

    /// <summary>The SVG for a pictogram image filename (served at /pictograms/{image}).</summary>
    public string? GetPictogramSvg(string image)
    {
        using var db = _dbf.CreateDbContext();
        return db.Pictograms.AsNoTracking().FirstOrDefault(p => p.Image == image)?.Svg;
    }

    // ---------- Writes ----------

    public void UpsertLabel(LabelStock label) => Upsert(db => db.Labels, label.Id, label);
    public bool DeleteLabel(string id) => Delete<LabelStock>(id);

    public void UpsertTemplate(LabelTemplate template) => Upsert(db => db.Templates, template.Id, template);
    public bool DeleteTemplate(string id) => Delete<LabelTemplate>(id);

    public void UpsertPrinter(Printer printer) => Upsert(db => db.Printers, printer.Id, printer);
    public bool DeletePrinter(string id) => Delete<Printer>(id);

    public void UpsertPictogram(Pictogram pictogram) => Upsert(db => db.Pictograms, pictogram.Name, pictogram);
    public bool DeletePictogram(string name) => Delete<Pictogram>(name);

    private void Upsert<T>(Func<LablrDbContext, DbSet<T>> set, string id, T entity) where T : class
    {
        using var db = _dbf.CreateDbContext();
        var dbSet = set(db);
        var exists = dbSet.Find(id) is not null;
        if (exists)
        {
            // Detach the loaded instance so Update(entity) can attach by the same key.
            db.ChangeTracker.Clear();
            dbSet.Update(entity); // marks all columns modified — full overwrite
        }
        else
        {
            dbSet.Add(entity);
        }
        db.SaveChanges();
    }

    private bool Delete<T>(string id) where T : class
    {
        using var db = _dbf.CreateDbContext();
        var entity = db.Set<T>().Find(id);
        if (entity is null) return false;
        db.Set<T>().Remove(entity);
        db.SaveChanges();
        return true;
    }

    // ---------- Seed drafts (RAM store) ----------

    /// <summary>Seed drafts shipped with the config (id = filename); data only.</summary>
    public IEnumerable<(string Id, Dictionary<string, string> Fields)> LoadSeedDrafts()
    {
        var path = Path.Combine(_seedDir, "drafts");
        if (!Directory.Exists(path)) yield break;
        foreach (var file in Directory.EnumerateFiles(path, "*.yaml").OrderBy(x => x))
        {
            Dictionary<string, string>? fields = null;
            try { fields = _yaml.Deserialize<DraftFile>(File.ReadAllText(file))?.Fields; }
            catch (Exception e) { _log.LogError(e, "Failed to parse draft {File}", file); }
            if (fields is { Count: > 0 })
                yield return (Path.GetFileNameWithoutExtension(file), fields);
        }
    }

    // ---------- First-boot seed ----------

    private void SeedIfEmpty(LablrDbContext db)
    {
        if (db.Labels.Any() || db.Templates.Any() || db.Printers.Any() || db.Pictograms.Any())
            return;
        if (!Directory.Exists(_seedDir))
        {
            _log.LogWarning("DB empty and no seed dir at {Dir}; starting blank", _seedDir);
            return;
        }

        db.Labels.AddRange(SeedDir<LabelStock>("labels"));
        db.Templates.AddRange(SeedDir<LabelTemplate>("templates"));
        db.Printers.AddRange(SeedDir<Printer>("printers"));
        db.Pictograms.AddRange(SeedPictograms());
        db.SaveChanges();

        _log.LogInformation(
            "Seeded DB from {Dir}: {L} labels, {T} templates, {P} printers, {G} pictograms",
            _seedDir, db.Labels.Count(), db.Templates.Count(), db.Printers.Count(), db.Pictograms.Count());
    }

    private List<T> SeedDir<T>(string subdir)
    {
        var path = Path.Combine(_seedDir, subdir);
        var list = new List<T>();
        if (!Directory.Exists(path)) return list;
        foreach (var file in Directory.EnumerateFiles(path, "*.yaml").OrderBy(x => x))
        {
            try
            {
                var item = _yaml.Deserialize<T>(File.ReadAllText(file));
                if (item != null) list.Add(item);
            }
            catch (Exception e) { _log.LogError(e, "Failed to seed {File}", file); }
        }
        return list;
    }

    private List<Pictogram> SeedPictograms()
    {
        var list = new List<Pictogram>();
        var registryFile = Path.Combine(_seedDir, "pictograms.yaml");
        if (!File.Exists(registryFile)) return list;

        Dictionary<string, PictogramRef> registry;
        try
        {
            registry = _yaml.Deserialize<PictogramFile>(File.ReadAllText(registryFile))?.Pictograms ?? new();
        }
        catch (Exception e)
        {
            _log.LogError(e, "Failed to parse {File}", registryFile);
            return list;
        }

        foreach (var (name, def) in registry)
        {
            var svgPath = Path.Combine(_seedDir, "pictograms", def.Image);
            if (!File.Exists(svgPath))
            {
                _log.LogWarning("Pictogram SVG missing: {Path}", svgPath);
                continue;
            }
            list.Add(new Pictogram { Name = name, Image = def.Image, Svg = File.ReadAllText(svgPath) });
        }
        return list;
    }
}

// --- YAML seed-file shapes (internal to seeding) ---

internal sealed class PictogramFile
{
    public Dictionary<string, PictogramRef> Pictograms { get; set; } = new();
}

internal sealed class DraftFile
{
    public Dictionary<string, string> Fields { get; set; } = new();
}
