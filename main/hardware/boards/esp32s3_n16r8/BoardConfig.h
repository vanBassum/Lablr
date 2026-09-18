#pragma once

// ──────────────────────────────────────────────────────────────
// BoardContext configuration — ESP32-S3, 16 MB flash, 8 MB octal PSRAM
// (an N16R8 module: ESP32-S3-WROOM-1-N16R8 and the DevKitC-1 built on it,
// and the clones that use the same part).
//
// The folder is named after the MODULE rather than a devkit, because the
// module variant is what is actually known about this hardware: 16 MB of
// quad flash and 8 MB of octal PSRAM. Pin assignments below are the part
// of a board that a module cannot tell you, which is why there are almost
// none here yet.
//
// Other boards live in sibling folders under hardware/boards/ and are
// selected with -DBOARD=<name>.
// ──────────────────────────────────────────────────────────────

namespace BoardConfig
{
    // LED - DELIBERATELY UNSET, and staying that way.
    //
    // This product has no LED and wants none: it drives a label printer, and
    // the only thing an indicator would report is a link state the printer's
    // own commands already answer. The template's LedManager demo that used
    // to make something of it is gone from main/app/ for that reason.
    //
    // `Led` is still a role every board owes (BoardProvider), so this board
    // binds MockLed: the role is satisfied and no pin is touched. Nothing
    // above the board reads it any more, and that member goes away the day
    // `Led` stops being one of the roles - which is a Strux decision, not
    // this product's, so it stays.
    //
    // So there is no LED_PIN here, and that is a decision rather than a gap.
    // The ESP32-S3-DevKitC-1's LED is an addressable WS2812 on GPIO38 or
    // GPIO48 depending on revision; writing that driver would be work spent
    // on the copy, not the product.
    //
    // If some later board genuinely needs one: add LED_PIN and
    // LED_ACTIVE_HIGH here and swap MockLed for GpioLed in BoardContext.h.

    // Add project-specific pin definitions below.
    //
    // Free and worth knowing on an S3 module, for whatever comes next:
    //   GPIO19/GPIO20   USB D-/D+ (native USB, taken once USB host is used)
    //   GPIO43/GPIO44   UART0 TX/RX, the default console
    //   GPIO0           BOOT strapping pin
    //   GPIO26-GPIO32   taken by the module's own flash/PSRAM - NEVER reuse
    //                   (an N16R8 uses the octal set, so treat 33-37 as
    //                    taken too)
}
