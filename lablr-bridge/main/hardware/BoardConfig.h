#pragma once

// ──────────────────────────────────────────────────────────────
// Board configuration — hardware-specific pin assignments and constants.
// Edit this file to match your board or target MCU.
// ──────────────────────────────────────────────────────────────

namespace BoardConfig
{
    // Plain single-colour GPIO LED. Disabled (-1) so we leave the pin in its
    // default high-impedance input state — that reads as "off" for an LED of
    // either polarity, instead of us actively driving (and lighting) it.
    static constexpr int LED_PIN = -1;
    static constexpr bool LED_ACTIVE_HIGH = true;

    // On-board addressable RGB LED (WS2812). Most ESP32-S3 DevKits put it on
    // GPIO48 — and it's blindingly bright. We send a one-shot "all off" frame
    // at boot to blank it. Set to -1 if your board has no RGB LED.
    static constexpr int RGB_LED_PIN = 48;

    // ── USB OTG (host mode, to the Dymo) ──────────────────────
    // The ESP32-S3 USB-OTG peripheral is fixed to GPIO19 (D-) and GPIO20 (D+);
    // they are not remappable, so there is nothing to configure here. Use the
    // board's native USB connector (not the UART/JTAG port) and supply 5 V to
    // the printer's VBUS from an external supply — the S3 cannot power a
    // LabelWriter itself.
    //
    // Add project-specific pin definitions below.
}
