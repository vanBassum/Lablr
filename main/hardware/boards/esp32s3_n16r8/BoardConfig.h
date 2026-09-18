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
    // LED — DELIBERATELY UNSET.
    //
    // There is no pin here because nobody has said which pin, and a guess
    // would be the kind that only fails on a bench. The ESP32-S3-DevKitC-1
    // carries an addressable RGB LED (WS2812) rather than a plain GPIO one,
    // on GPIO38 or GPIO48 depending on the board revision, and driving it
    // needs a driver this repository does not have. A clone may have a plain
    // LED somewhere else entirely, or none.
    //
    // So this board binds MockLed in its BoardContext (see Led.h): the role
    // is satisfied, the LED demo runs and reports its state, and nothing
    // lights up. When the real LED is known:
    //
    //   plain GPIO LED  ->  add LED_PIN / LED_ACTIVE_HIGH here and swap
    //                       MockLed for GpioLed in BoardContext.h,
    //   addressable     ->  write the driver in hardware/drivers/ first.

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
