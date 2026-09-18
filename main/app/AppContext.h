#pragma once

#include "AppProvider.h"
#include "BoardContext.h"
#include "StruxProvider.h"
#include "SystemManager.h"
#include "DeviceDoc.h"
#include "LedManager/LedManager.h"
#include "StorageManager/StorageManager.h"
#include "RenderManager/RenderManager.h"
#include "UsbHostManager/UsbHostManager.h"
#include "PrintManager/PrintManager.h"

// The application layer's context: owns this product's managers and answers AppProvider.
//
// This is the file a fork edits. Strux's own managers, their init order and their wiring
// all live one layer down in StruxContext, so pulling an improvement from the template
// does not touch anything here — which is the whole reason the layers were split.
//
// Adding an application manager means: create the class taking AppProvider&, add an
// accessor to AppProvider, add the member here, and call its Init() below. The framework
// does not need to be told it exists; the manager registers its own commands and
// settings into Strux from its Init().
class AppContext : public AppProvider
{
public:
    AppContext(BoardContext& board, StruxProvider& strux)
        : board_(board), strux_(strux) {}

    ~AppContext() = default;
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    /// Bring the application up. Called last: every manager here registers into the
    /// framework, so the framework has to be ready before any of this runs.
    void Init()
    {
        // What this product is, in the product's own words — registered like
        // everything else the application tells the framework about itself, and read
        // back by `system describe` and by the relay hello. See DeviceDoc.h.
        strux_.getSystemManager().SetDocumentation(
            DeviceDoc::DESCRIPTION, DeviceDoc::INSTRUCTIONS);

        ledManager_.Init();

        // Storage before render: RenderManager reads /fonts at Init, and a
        // renderer that came up first would register no fonts and say so
        // only in a log line nobody reads until a label has no text.
        storageManager_.Init();
        renderManager_.Init();

        // The USB host is independent of the other three: it waits for a device
        // to be plugged in and answers for whatever turns up.
        usbHostManager_.Init();

        // Last: it reaches the renderer and the USB host through the provider,
        // and both have to be able to answer before a print command can arrive.
        printManager_.Init();
    }

    StruxProvider& getStrux() override { return strux_; }
    BoardContext& getBoard() override { return board_; }
    LedManager& getLedManager() override { return ledManager_; }
    StorageManager& getStorageManager() override { return storageManager_; }
    RenderManager& getRenderManager() override { return renderManager_; }
    UsbHostManager& getUsbHostManager() override { return usbHostManager_; }
    PrintManager& getPrintManager() override { return printManager_; }

private:
    BoardContext& board_;
    StruxProvider& strux_;

    LedManager ledManager_{*this};
    StorageManager storageManager_{*this};
    RenderManager renderManager_{*this};
    UsbHostManager usbHostManager_{*this};
    PrintManager printManager_{*this};
};
