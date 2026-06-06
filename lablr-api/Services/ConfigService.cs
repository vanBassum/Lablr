using System.Text.RegularExpressions;
using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Infrastructure;
using Microsoft.EntityFrameworkCore.Migrations;
using Microsoft.EntityFrameworkCore.Storage;
using YamlDotNet.Serialization;
using YamlDotNet.Serialization.NamingConventions;

namespace Lablr.Api.Services;

/// <summary>Thrown when a write is rejected; surfaced as 400 (REST) or a tool error (MCP).</summary>
public sealed class ConfigValidationException(string message) : Exception(message);

/// <summary>
/// The config store. Models (labels, templates, printers, pictograms) live in
/// SQLite via EF Core and are edited at runtime (REST/MCP). Validation lives here
/// so both surfaces share it. On first boot an empty DB is seeded from the YAML in
/// Config:Dir; after that the DB is the source of truth. Drafts are seeded into
/// the RAM store from the same YAML on every boot (ephemeral, not in the DB).
/// </summary>
public sealed class ConfigService
{
    private readonly IDbContextFactory<LablrDbContext> _dbf;
    private readonly string _seedDir;
    private readonly ILogger<ConfigService> _log;

    // Bumped on every config write; used to invalidate render caches/ETags.
    private long _version;
    public long ConfigVersion => Interlocked.Read(ref _version);
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
        MigrateToLatest(db);
        SeedIfEmpty(db);
    }

    /// <summary>
    /// Bring the schema up to date via EF migrations. A DB created by the old
    /// <c>EnsureCreated()</c> path has our tables but no <c>__EFMigrationsHistory</c>;
    /// stamp the existing migrations as already-applied (a one-time baseline) so
    /// <see cref="RelationalDatabaseFacadeExtensions.Migrate"/> won't try to recreate
    /// them. A fresh/empty DB has no baseline to do and is built straight from migrations.
    /// </summary>
    private void MigrateToLatest(LablrDbContext db)
    {
        var creator = db.GetService<IRelationalDatabaseCreator>();
        if (creator.Exists() && creator.HasTables() && !db.Database.GetAppliedMigrations().Any())
        {
            var history = db.GetService<IHistoryRepository>();
            db.Database.ExecuteSqlRaw(history.GetCreateIfNotExistsScript());
            foreach (var id in db.Database.GetMigrations())
                db.Database.ExecuteSqlRaw(history.GetInsertScript(new HistoryRow(id, ProductInfo.GetVersion())));
            _log.LogInformation("Baselined pre-migrations config DB onto EF migrations");
        }
        db.Database.Migrate();
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

    public List<LabelStock> GetLabels() => All<LabelStock>();
    public LabelStock? GetLabel(string id) => Find<LabelStock>(id);

    public List<LabelTemplate> GetTemplates() => All<LabelTemplate>();
    public LabelTemplate? GetTemplate(string id) => Find<LabelTemplate>(id);

    public List<Printer> GetPrinters() => All<Printer>();
    public Printer? GetPrinter(string id) => Find<Printer>(id);

    public List<Pictogram> GetPictograms() => All<Pictogram>();
    public Pictogram? GetPictogram(string name) => Find<Pictogram>(name);

    /// <summary>The SVG for a pictogram image filename (served at /pictograms/{image}).</summary>
    public string? GetPictogramSvg(string image)
    {
        using var db = _dbf.CreateDbContext();
        return db.Pictograms.AsNoTracking().FirstOrDefault(p => p.Image == image)?.Svg;
    }

    private List<T> All<T>() where T : class
    {
        using var db = _dbf.CreateDbContext();
        return db.Set<T>().AsNoTracking().ToList();
    }

    private T? Find<T>(string id) where T : class
    {
        using var db = _dbf.CreateDbContext();
        return db.Set<T>().Find(id) is { } e ? Detached(db, e) : null;
    }

    private static T Detached<T>(LablrDbContext db, T entity) where T : class
    {
        db.Entry(entity).State = EntityState.Detached;
        return entity;
    }

    // ---------- Writes (validated; throw ConfigValidationException) ----------

    public LabelStock UpsertLabel(LabelStock label)
    {
        Require(label.Id, "label.id");
        Require(label.Name, "label.name");
        if (label.WidthMm <= 0 || label.HeightMm <= 0)
            throw new ConfigValidationException("label.widthMm and label.heightMm must be greater than 0.");
        Save(db => db.Labels, label.Id, label);
        return label;
    }

    public bool DeleteLabel(string id) => Delete<LabelStock>(id);

    public LabelTemplate UpsertTemplate(LabelTemplate template)
    {
        Require(template.Id, "template.id");
        Require(template.Name, "template.name");
        if (Find<LabelStock>(template.Label) is null)
            throw new ConfigValidationException(
                $"Unknown label '{template.Label}'. Create it first (POST /api/labels or upsert_label).");
        var hasElements = template.Elements is { Count: > 0 };
        var hasVariants = template.Variants is { Count: > 0 };
        if (!hasElements && !hasVariants)
            throw new ConfigValidationException("Provide either `elements` or `variants`.");
        Save(db => db.Templates, template.Id, template);
        return template;
    }

    public bool DeleteTemplate(string id) => Delete<LabelTemplate>(id);

    public Printer UpsertPrinter(Printer printer)
    {
        Require(printer.Id, "printer.id");
        if (printer.Dpi <= 0) throw new ConfigValidationException("printer.dpi must be greater than 0.");
        Save(db => db.Printers, printer.Id, printer);
        return printer;
    }

    public bool DeletePrinter(string id) => Delete<Printer>(id);

    public Pictogram UpsertPictogram(string name, string svg)
    {
        Require(name, "name");
        if (string.IsNullOrWhiteSpace(svg) || !svg.Contains("<svg"))
            throw new ConfigValidationException("svg must be SVG markup containing an <svg> element.");
        // Keep an existing pictogram's filename so its URL stays stable; else derive one.
        var image = GetPictogram(name)?.Image ?? Slug(name) + ".svg";
        var pictogram = new Pictogram { Name = name, Image = image, Svg = svg };
        Save(db => db.Pictograms, name, pictogram);
        return pictogram;
    }

    public bool DeletePictogram(string name) => Delete<Pictogram>(name);

    /// <summary>Validate a draft against a template; throws if unknown or missing required fields.</summary>
    public void ValidateDraft(string templateId, IDictionary<string, string> fields)
    {
        var template = GetTemplate(templateId)
            ?? throw new ConfigValidationException($"Unknown template '{templateId}'.");
        var missing = template.RequiredFields
            .Where(f => !fields.TryGetValue(f, out var v) || string.IsNullOrWhiteSpace(v))
            .ToList();
        if (missing.Count > 0)
            throw new ConfigValidationException(
                $"Missing required fields for '{templateId}': {string.Join(", ", missing)}");
    }

    private static void Require(string value, string field)
    {
        if (string.IsNullOrWhiteSpace(value)) throw new ConfigValidationException($"{field} is required.");
    }

    private static string Slug(string s) => Regex.Replace(s.ToLowerInvariant(), "[^a-z0-9]+", "-").Trim('-');

    private void Save<T>(Func<LablrDbContext, DbSet<T>> set, string id, T entity) where T : class
    {
        using var db = _dbf.CreateDbContext();
        var dbSet = set(db);
        if (dbSet.Find(id) is not null)
        {
            db.ChangeTracker.Clear(); // detach the loaded copy so Update can attach by key
            dbSet.Update(entity);     // marks all columns modified — full overwrite
        }
        else
        {
            dbSet.Add(entity);
        }
        db.SaveChanges();
        Interlocked.Increment(ref _version);
    }

    private bool Delete<T>(string id) where T : class
    {
        using var db = _dbf.CreateDbContext();
        var entity = db.Set<T>().Find(id);
        if (entity is null) return false;
        db.Set<T>().Remove(entity);
        db.SaveChanges();
        Interlocked.Increment(ref _version);
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
