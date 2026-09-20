#pragma once

#include <cstdint>
#include <cstddef>

#include "esp_heap_caps.h"
#include "qrcode.h"

#include "SvgQrCode.h"

// --------------------------------------------------------------
// The device half of the QR placeholder pass: espressif/qrcode behind the
// svg::QrEncoder interface that lib/common/SvgQrCode.h draws from.
//
// The split exists so the expansion - parsing the placeholder, snapping the
// module size, centring, emitting the path - stays pure and host-testable,
// while the part that genuinely needs the device (a real Reed-Solomon encoder)
// lives up here in the application with its dependency.
//
// Two things the component's API forces:
//
//   - The handle is only valid INSIDE the display callback; the buffers are
//     freed as soon as esp_qrcode_generate returns. So the callback copies the
//     modules out rather than keeping the handle.
//   - display_func and display_func_with_cb are a union, and the library picks
//     the second only when user_data is non-null. Passing `this` is what makes
//     the callback a method call.
//
// The module buffer is one byte per module in PSRAM, allocated on first use, so
// a render with no QR in it pays nothing. A byte rather than a bit because at
// 13 KB in PSRAM the packing would buy nothing and cost a shift per lookup, in
// a loop the expansion runs over every module twice.
// --------------------------------------------------------------

class EspQrEncoder final : public svg::QrEncoder
{
public:
    EspQrEncoder() = default;
    ~EspQrEncoder() override { if (modules_) heap_caps_free(modules_); }

    EspQrEncoder(const EspQrEncoder&)            = delete;
    EspQrEncoder& operator=(const EspQrEncoder&) = delete;

    int Encode(const char* text, size_t len, svg::QrEcc ecc) override
    {
        (void)len;                       // the component takes a C string
        size_ = 0;

        esp_qrcode_config_t cfg = {};
        cfg.display_func_with_cb = &EspQrEncoder::OnEncoded;
        cfg.max_qrcode_version   = MAX_VERSION;
        cfg.qrcode_ecc_level     = ToEspEcc(ecc);
        cfg.user_data            = this;

        if (esp_qrcode_generate(&cfg, text) != ESP_OK) return 0;
        return size_;
    }

    bool Module(int x, int y) const override
    {
        if (!modules_ || x < 0 || y < 0 || x >= size_ || y >= size_) return false;
        return modules_[y * size_ + x] != 0;
    }

private:
    /// Version 25 holds around a kilobyte at ECC M - far more than a label's
    /// URL - and caps what the component transiently allocates on the internal
    /// heap to about 3.4 KB rather than the 7.8 KB version 40 would take.
    static constexpr int MAX_VERSION = 25;
    static constexpr int MAX_SIDE    = 4 * MAX_VERSION + 17;   // 117 modules

    static void OnEncoded(esp_qrcode_handle_t qrcode, void* user)
    {
        static_cast<EspQrEncoder*>(user)->Store(qrcode);
    }

    void Store(esp_qrcode_handle_t qrcode)
    {
        const int n = esp_qrcode_get_size(qrcode);
        if (n <= 0 || n > MAX_SIDE) return;

        if (!modules_)
        {
            modules_ = static_cast<uint8_t*>(
                heap_caps_malloc(static_cast<size_t>(MAX_SIDE) * MAX_SIDE,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!modules_) return;      // size_ stays 0, which the pass reports
        }

        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                modules_[y * n + x] = esp_qrcode_get_module(qrcode, x, y) ? 1u : 0u;

        size_ = n;
    }

    static int ToEspEcc(svg::QrEcc ecc)
    {
        switch (ecc)
        {
            case svg::QrEcc::Low:      return ESP_QRCODE_ECC_LOW;
            case svg::QrEcc::Quartile: return ESP_QRCODE_ECC_QUART;
            case svg::QrEcc::High:     return ESP_QRCODE_ECC_HIGH;
            case svg::QrEcc::Medium:   break;
        }
        return ESP_QRCODE_ECC_MED;
    }

    uint8_t* modules_ = nullptr;
    int      size_    = 0;
};
