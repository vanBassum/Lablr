#pragma once

// What one application manager may reach for: its peers, the framework beneath it, and
// the board beneath that. Implemented by AppContext and handed to every app
// manager at construction — the same shape as StruxProvider one layer down, so a manager
// still takes exactly one reference and finds everything through it.
//
// The two extra accessors are what make this the top layer: getStrux() reaches down to
// the framework (register a command, read a setting, take a telemetry point) and
// getBoard() reaches past it to the hardware. Neither points back up, and nothing in
// Strux or on the BoardContext can see this interface at all.

class BoardContext;
class StruxProvider;
class LedManager;
class StorageManager;
class RenderManager;
class UsbHostManager;
class MediaManager;
class PrintManager;

class AppProvider
{
public:
    /// The framework layer. Everything Strux offers is behind this one call.
    virtual StruxProvider& getStrux() = 0;

    /// The hardware. Only the application layer has this — see StruxProvider.h for why.
    virtual BoardContext& getBoard() = 0;

    // ── This application's own managers ──
    virtual LedManager& getLedManager() = 0;

    /// The label filesystem: /labels, /fonts, /media.
    virtual StorageManager& getStorageManager() = 0;

    /// SVG to bitmap. Reads what StorageManager holds, which is why it
    /// initialises after it.
    virtual RenderManager& getRenderManager() = 0;

    /// The USB OTG port in host mode. Knows nothing about printing - see
    /// UsbHostManager.h for why that split is here.
    virtual UsbHostManager& getUsbHostManager() = 0;

    /// What the physical label stock IS - /media, one definition per roll,
    /// written at runtime. Describes paper only; the printer's DPI and head
    /// width are PrintManager's.
    virtual MediaManager& getMediaManager() = 0;

    /// The printer's raster dialect. Draws through RenderManager, sizes through
    /// MediaManager and sends through UsbHostManager, and owns none of them.
    virtual PrintManager& getPrintManager() = 0;
};
