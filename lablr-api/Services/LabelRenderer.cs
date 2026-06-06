using SkiaSharp;
using Svg.Skia;

namespace Lablr.Api.Services;

/// <summary>
/// The single label renderer. It produces BOTH the preview image and the printer
/// bytes from one render path, so preview == print by construction — the frontend
/// does not render, it fetches this output. Text shrink-to-fit/wrap/align/valign
/// with a margin safe-area, plus SVG pictograms; thresholded to 1-bit and packed
/// into a DYMO LabelWriter 450 raster job.
/// </summary>
public sealed class LabelRenderer
{
    private const int HeadDots = 672;          // LW450 full head width (84 bytes * 8)
    private const double LineHeight = 1.2;

    private readonly ConfigService _config;
    private readonly SKTypeface _sans;
    private readonly SKTypeface _sansBold;

    public LabelRenderer(ConfigService config)
    {
        _config = config;
        _sans = SKTypeface.FromFamilyName("DejaVu Sans") ?? SKTypeface.Default;
        _sansBold = SKTypeface.FromFamilyName("DejaVu Sans",
            SKFontStyleWeight.Bold, SKFontStyleWidth.Normal, SKFontStyleSlant.Upright) ?? _sans;
    }

    /// <summary>
    /// Render the label bitmap in its design (reading) orientation — no head
    /// placement, no offset. This is the exact image the preview shows. Caller disposes.
    /// </summary>
    public SKBitmap RenderLabel(
        LabelTemplate template, IReadOnlyDictionary<string, string> fields,
        LabelStock stock, Printer printer, string orientation)
    {
        double mmToDots = printer.Dpi / 25.4;
        bool landscape = orientation == "landscape";
        double wMm = landscape ? stock.HeightMm : stock.WidthMm;
        double hMm = landscape ? stock.WidthMm : stock.HeightMm;
        int w = (int)Math.Round(wMm * mmToDots);
        int h = (int)Math.Round(hMm * mmToDots);

        var label = new SKBitmap(Math.Max(1, w), Math.Max(1, h));
        using (var canvas = new SKCanvas(label))
        {
            canvas.Clear(SKColors.White);
            DrawElements(canvas, ElementsFor(template, orientation), fields, stock, mmToDots, w, h);
        }
        return label;
    }

    /// <summary>
    /// The preview image: the label as a 1-bit PNG. These are the same pixels the
    /// printer receives (before the print-time head placement), so what you see is
    /// what prints.
    /// </summary>
    public byte[] RenderPreviewPng(
        LabelTemplate template, IReadOnlyDictionary<string, string> fields,
        LabelStock stock, Printer printer, string orientation)
    {
        using var label = RenderLabel(template, fields, stock, printer, orientation);
        using var mono = Threshold1Bit(label);
        using var img = SKImage.FromBitmap(mono);
        using var data = img.Encode(SKEncodedImageFormat.Png, 100);
        return data.ToArray();
    }

    /// <summary>
    /// Render a draft+template to a complete LW450 job (bytes for the bridge / USB).
    /// <paramref name="nudgeX"/>/<paramref name="nudgeY"/> are extra head-placement
    /// dots (manual calibration), added on top of the stock's offset.
    /// </summary>
    public byte[] RenderJob(
        LabelTemplate template, IReadOnlyDictionary<string, string> fields,
        LabelStock stock, Printer printer, string orientation, int nudgeX = 0, int nudgeY = 0)
    {
        double mmToDots = printer.Dpi / 25.4;
        bool landscape = orientation == "landscape";
        using var label = RenderLabel(template, fields, stock, printer, orientation);

        // Landscape is authored wide; rotate it 90° CW onto the physical media.
        SKBitmap oriented = landscape ? Rotate90Cw(label) : label;
        try
        {
            int offX = (int)Math.Round((stock.OffsetCorrectionMm?.X ?? 0) * mmToDots) + nudgeX;
            int offY = (int)Math.Round((stock.OffsetCorrectionMm?.Y ?? 0) * mmToDots) + nudgeY;
            int headH = oriented.Height + Math.Max(0, offY);

            using var head = new SKBitmap(HeadDots, Math.Max(1, headH));
            using (var canvas = new SKCanvas(head))
            {
                canvas.Clear(SKColors.White);
                canvas.DrawBitmap(oriented, offX, offY);
            }
            return BuildJob(head);
        }
        finally
        {
            if (landscape) oriented.Dispose();
        }
    }

    // ── Element layout (mirrors render.ts) ───────────────────────

    private void DrawElements(
        SKCanvas canvas, List<TemplateElement> elements, IReadOnlyDictionary<string, string> fields,
        LabelStock stock, double mmToDots, int w, int h)
    {
        var m = stock.MarginsMm;
        double safeX0 = (m?.Left ?? 0) * mmToDots;
        double safeY0 = (m?.Top ?? 0) * mmToDots;
        double safeX1 = w - (m?.Right ?? 0) * mmToDots;
        double safeY1 = h - (m?.Bottom ?? 0) * mmToDots;

        foreach (var el in elements)
        {
            if (!fields.TryGetValue(el.Field, out var value) || string.IsNullOrEmpty(value))
                continue;

            if (el.Type == "pictogram")
            {
                DrawPictogram(canvas, value,
                    el.Rect.X * mmToDots, el.Rect.Y * mmToDots,
                    el.Rect.Width * mmToDots, el.Rect.Height * mmToDots);
                continue;
            }

            double x0 = Math.Max(el.Rect.X * mmToDots, safeX0);
            double y0 = Math.Max(el.Rect.Y * mmToDots, safeY0);
            double x1 = Math.Min((el.Rect.X + el.Rect.Width) * mmToDots, safeX1);
            double y1 = Math.Min((el.Rect.Y + el.Rect.Height) * mmToDots, safeY1);
            if (x1 - x0 <= 0 || y1 - y0 <= 0) continue;

            DrawText(canvas, value, el, x0, y0, x1 - x0, y1 - y0, mmToDots);
        }
    }

    private void DrawPictogram(SKCanvas canvas, string name, double rx, double ry, double rw, double rh)
    {
        var svg = _config.GetPictogram(name)?.Svg;
        if (string.IsNullOrEmpty(svg)) return;
        using var pic = new SKSvg();
        if (pic.FromSvg(svg) is null || pic.Picture is null) return;

        var bounds = pic.Picture.CullRect;
        if (bounds.Width <= 0 || bounds.Height <= 0) return;

        double side = Math.Min(rw, rh);
        float scale = (float)(side / Math.Max(bounds.Width, bounds.Height));
        float dx = (float)(rx + (rw - side) / 2);
        float dy = (float)(ry + (rh - side) / 2);

        var matrix = SKMatrix.CreateScaleTranslation(scale, scale, dx, dy);
        canvas.DrawPicture(pic.Picture, ref matrix);
    }

    private void DrawText(
        SKCanvas canvas, string value, TemplateElement el,
        double rx, double ry, double rw, double rh, double mmToDots)
    {
        bool bold = el.Font?.Weight == "bold";
        using var paint = new SKPaint
        {
            IsAntialias = true,
            Color = SKColors.Black,
            Typeface = bold ? _sansBold : _sans,
        };

        var (size, lines, lineH) = FitText(paint, value, el, rw, rh, mmToDots);
        if (size <= 0) return;

        paint.TextSize = (float)size;
        paint.TextAlign = el.Align switch
        {
            "center" => SKTextAlign.Center,
            "right" => SKTextAlign.Right,
            _ => SKTextAlign.Left,
        };
        double x = el.Align == "center" ? rx + rw / 2 : el.Align == "right" ? rx + rw : rx;

        double blockH = lines.Count * lineH;
        double startY = el.Valign switch
        {
            "center" => ry + (rh - blockH) / 2,
            "bottom" => ry + rh - blockH,
            _ => ry,
        };

        var metrics = paint.FontMetrics;
        for (int i = 0; i < lines.Count; i++)
        {
            double lineTop = startY + i * lineH;
            canvas.DrawText(lines[i], (float)x, (float)(lineTop - metrics.Ascent), paint);
        }
    }

    private (double size, List<string> lines, double lineH) FitText(
        SKPaint paint, string text, TemplateElement el, double rw, double rh, double mmToDots)
    {
        double maxPx = (el.Font?.MaxSizeMm ?? 3) * mmToDots;
        double minPx = (el.Font?.MinSizeMm ?? 1.5) * mmToDots;
        bool wrap = el.Wrap == true;
        int? maxLines = el.MaxLines;
        double absFloor = 0.8 * mmToDots;
        double floorPx = el.Fit == "shrink" ? Math.Min(minPx, absFloor) : minPx;

        List<string> Layout()
        {
            paint.TextSize = (float)paint.TextSize;
            return wrap ? WrapText(paint, text, rw) : new List<string> { text };
        }

        if (el.Fit != "shrink")
        {
            paint.TextSize = (float)maxPx;
            return (maxPx, ClampLines(Layout(), maxLines), maxPx * LineHeight);
        }

        for (double size = maxPx; size >= floorPx; size -= 0.5)
        {
            paint.TextSize = (float)size;
            var lines = Layout();
            bool widthOk = lines.All(l => paint.MeasureText(l) <= rw);
            bool linesOk = maxLines is null || lines.Count <= maxLines;
            bool heightOk = lines.Count * size * LineHeight <= rh;
            if (widthOk && linesOk && heightOk) return (size, lines, size * LineHeight);
        }

        paint.TextSize = (float)floorPx;
        return (floorPx, ClampLines(Layout(), maxLines), floorPx * LineHeight);
    }

    private static List<string> ClampLines(List<string> lines, int? maxLines)
    {
        if (maxLines is null || lines.Count <= maxLines) return lines;
        var kept = lines.Take(maxLines.Value).ToList();
        kept[^1] = System.Text.RegularExpressions.Regex.Replace(kept[^1], @"\s+\S*$", "") + "…";
        return kept;
    }

    private static List<string> WrapText(SKPaint paint, string text, double maxW)
    {
        var words = text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        if (words.Length == 0) return new List<string> { text };
        var lines = new List<string>();
        var current = words[0];
        for (int i = 1; i < words.Length; i++)
        {
            var candidate = $"{current} {words[i]}";
            if (paint.MeasureText(candidate) <= maxW) current = candidate;
            else { lines.Add(current); current = words[i]; }
        }
        lines.Add(current);
        return lines;
    }

    // ── 1-bit raster + LW450 job (mirrors dymo.ts) ───────────────

    private static SKBitmap Rotate90Cw(SKBitmap src)
    {
        var dst = new SKBitmap(src.Height, src.Width);
        using var canvas = new SKCanvas(dst);
        canvas.Translate(dst.Width, 0);
        canvas.RotateDegrees(90);
        canvas.DrawBitmap(src, 0, 0);
        return dst;
    }

    /// <summary>Snap every pixel to pure black/white using the same rule as the
    /// printer pack (luminance &lt; 128 = black), so the preview matches the print.</summary>
    private static SKBitmap Threshold1Bit(SKBitmap src)
    {
        var dst = new SKBitmap(src.Width, src.Height);
        for (int y = 0; y < src.Height; y++)
            for (int x = 0; x < src.Width; x++)
            {
                var c = src.GetPixel(x, y);
                int lum = (c.Red + c.Green + c.Blue) / 3;
                bool black = c.Alpha != 0 && lum < 128;
                dst.SetPixel(x, y, black ? SKColors.Black : SKColors.White);
            }
        return dst;
    }

    private static byte[] BuildJob(SKBitmap head)
    {
        const byte ESC = 0x1b, SYN = 0x16;
        int w = head.Width, h = head.Height;
        int bytesPerLine = (w + 7) / 8;

        var job = new List<byte>(110 + h * (1 + bytesPerLine) + 2);
        for (int i = 0; i < 100; i++) job.Add(ESC);     // flush any partial command
        job.Add(ESC); job.Add(0x40);                     // ESC @  reset
        job.Add(ESC); job.Add(0x4c); job.Add((byte)((h >> 8) & 0xff)); job.Add((byte)(h & 0xff)); // ESC L len
        job.Add(ESC); job.Add(0x44); job.Add((byte)bytesPerLine);                                  // ESC D bytes/line

        for (int y = 0; y < h; y++)
        {
            job.Add(SYN);
            for (int b = 0; b < bytesPerLine; b++)
            {
                byte v = 0;
                for (int bit = 0; bit < 8; bit++)
                {
                    int x = b * 8 + bit;
                    if (x >= w) continue;
                    var c = head.GetPixel(x, y);
                    int lum = (c.Red + c.Green + c.Blue) / 3;
                    bool black = c.Alpha != 0 && lum < 128;
                    if (black) v |= (byte)(0x80 >> bit);
                }
                job.Add(v);
            }
        }

        job.Add(ESC); job.Add(0x45);                     // ESC E  form feed / eject
        return job.ToArray();
    }

    // ── Helpers shared with the UI's config logic ────────────────

    /// <summary>The elements to render for an orientation (handles both layouts).</summary>
    public static List<TemplateElement> ElementsFor(LabelTemplate t, string orientation)
    {
        if (t.Variants is { Count: > 0 })
        {
            if (t.Variants.TryGetValue(orientation, out var v) && v.Elements.Count > 0) return v.Elements;
            if (t.Variants.TryGetValue("portrait", out var p)) return p.Elements;
            if (t.Variants.TryGetValue("landscape", out var l)) return l.Elements;
            return new();
        }
        return t.Elements ?? new();
    }

    /// <summary>Default orientation for a template (first variant key, or its orientation).</summary>
    public static string DefaultOrientation(LabelTemplate t)
    {
        if (t.Variants is { Count: > 0 })
            return t.Variants.ContainsKey("portrait") ? "portrait"
                 : t.Variants.Keys.First();
        return t.Orientation ?? "portrait";
    }
}
