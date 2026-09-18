#include "ConsoleManager.h"
#include "CommandManager.h"
#include "JsonWriter.h"
#include "ReplyWriter.h"
#include "BufferStream.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <cstdio>
#include <cstring>
#include <cassert>

// This manager puts one function between every task in the system and the console
// (esp_log_set_vprintf, below), and LogOutput starts by calling vprintf. So the
// console's write must never block — if it does, a log line stalls whichever task
// emitted it, and that includes the Wi-Fi and lwIP tasks.
//
// USB Serial/JTAG as the PRIMARY console is a blocking VFS driver: its write waits
// for FIFO space, which means it waits for a USB host. That configuration cost this
// project a day of investigation and presented as 78 % packet loss with every
// command counter reading clean — see
// docs/reasoning/2026-09-09-20h40-a-console-that-waits-is-a-network-that-drops.md.
//
// It is caught here rather than left to a comment in a board file, because a comment
// is what failed the first time. USB output is not the problem and is not being
// refused: pick it as the SECONDARY console
// (CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG), which is a best-effort ROM path
// that drops instead of waiting, and every byte still reaches the USB port.
#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) && !defined(STRUX_ALLOW_BLOCKING_CONSOLE)
#error "USB Serial/JTAG is the PRIMARY console, whose write blocks on the USB host. \
Every task's log line goes through ConsoleManager::LogOutput -> vprintf, so this \
stalls the Wi-Fi and lwIP tasks and shows up as heavy packet loss. Use \
CONFIG_ESP_CONSOLE_UART_DEFAULT=y with \
CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y instead: USB still carries all \
output. Define STRUX_ALLOW_BLOCKING_CONSOLE only if you have measured that your \
build can afford it."
#endif

ConsoleManager* ConsoleManager::s_instance_ = nullptr;

struct LogQueueItem {
    char text[ConsoleManager::MAX_LINE_LEN];
};

ConsoleManager::ConsoleManager(StruxProvider& strux)
    : strux_(strux)
{
}

void ConsoleManager::Init()
{
    auto initAttempt = initState_.TryBeginInit();
    if (!initAttempt)
    {
        return;
    }

    s_instance_ = this;

    // Allocate log ring buffer in PSRAM (40KB)
    lines_ = static_cast<char (*)[MAX_LINE_LEN]>(
        heap_caps_calloc(MAX_LINES, MAX_LINE_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!lines_)
        lines_ = static_cast<char (*)[MAX_LINE_LEN]>(calloc(MAX_LINES, MAX_LINE_LEN));
    assert(lines_ && "Failed to allocate log buffer");

    queue_ = xQueueCreate(QUEUE_DEPTH, sizeof(LogQueueItem));

    broadcastTask_.Init("ConsoleBroadcast", 5, 4096);
    broadcastTask_.SetHandler([this]() { BroadcastTaskLoop(); });
    broadcastTask_.Run();

    esp_log_set_vprintf(&LogOutput);

    // ConsoleManager initializes before CommandManager::Init() — fine by
    // design: the registry is usable from construction.
    strux_.getCommandManager().Register(this, commands_);

    initAttempt.SetReady();
    ESP_LOGI(TAG, "Initialized (capturing stdout)");
}

void ConsoleManager::SetBroadcastCallback(BroadcastFunc func, void* ctx)
{
    broadcastFunc_ = func;
    broadcastCtx_ = ctx;
}

int ConsoleManager::LogOutput(const char* fmt, va_list args)
{
    int ret = vprintf(fmt, args);

    if (!s_instance_) return ret;

    // Format into a temp buffer
    char buf[MAX_LINE_LEN];
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(buf, sizeof(buf), fmt, args_copy);
    va_end(args_copy);

    if (len <= 0) return ret;
    if (len >= static_cast<int>(sizeof(buf))) len = sizeof(buf) - 1;

    // Accumulate into lineBuf_, flush on newline
    auto* self = s_instance_;
    for (int i = 0; i < len; i++)
    {
        char c = buf[i];
        if (c == '\n')
        {
            self->FlushLine();
        }
        else if (c != '\r')
        {
            if (self->lineLen_ < MAX_LINE_LEN - 1)
            {
                self->lineBuf_[self->lineLen_++] = c;
            }
        }
    }

    return ret;
}

void ConsoleManager::FlushLine()
{
    if (lineLen_ == 0) return;

    lineBuf_[lineLen_] = '\0';

    StoreLine(lineBuf_, lineLen_);

    // See broadcastTaskHandle_: anything logged while a broadcast is in flight is a
    // side effect of that broadcast, and queueing it is what turns one dead socket into
    // a flood. It is still stored above, so `log list` and the serial console keep it.
    if (queue_ && xTaskGetCurrentTaskHandle() != broadcastTaskHandle_.load())
    {
        LogQueueItem item = {};
        snprintf(item.text, sizeof(item.text), "%s", lineBuf_);
        xQueueSend(queue_, &item, 0);
    }

    lineLen_ = 0;
}

void ConsoleManager::StoreLine(const char* line, int32_t len)
{
    LOCK(mutex_);
    if (len >= MAX_LINE_LEN) len = MAX_LINE_LEN - 1;
    memcpy(lines_[head_], line, len);
    lines_[head_][len] = '\0';
    head_ = (head_ + 1) % MAX_LINES;
    if (count_ < MAX_LINES) count_++;
}

void ConsoleManager::BroadcastTaskLoop()
{
    LogQueueItem item;

    // Learned here rather than from Task, which does not expose its handle — and this
    // is the one place that is certainly running on it.
    broadcastTaskHandle_.store(xTaskGetCurrentTaskHandle());

    while (true)
    {
        if (xQueueReceive(queue_, &item, portMAX_DELAY) != pdTRUE)
            continue;

        if (!broadcastFunc_)
            continue;

        char jsonBuf[MAX_LINE_LEN + 32];
        BufferStream stream(jsonBuf, sizeof(jsonBuf));
        JsonWriter json(stream);
        json.beginObject();
        json.field("log", item.text);
        json.endObject();

        broadcastFunc_(stream.data(), stream.length(), broadcastCtx_);
    }
}

void ConsoleManager::WriteHistory(ReplyObject& resp) const
{
    // The lock is NOT held across the writes to `resp`, and that is the whole point
    // of the shape below. `resp` goes to the transport on every value, so writing 200
    // lines under this mutex holds it for the length of a 40 KB network reply — while
    // StoreLine takes the same mutex from whatever task just logged, which includes
    // the Wi-Fi and lwIP tasks. Opening the Console page could therefore stall the
    // network stack for as long as its own reply took to send.
    //
    // That is the same defect as the blocking console this file guards against at the
    // top: a network-speed operation blocking a task that only wanted to log a line.
    // So snapshot the bounds, then copy one line at a time and release between lines.
    int32_t start = 0;
    int32_t total = 0;
    {
        LOCK(mutex_);
        total = count_;
        start = (count_ < MAX_LINES) ? 0 : head_;
    }

    auto lines = resp.array("lines");

    for (int32_t i = 0; i < total; i++)
    {
        // One line, one short lock. If the ring wraps while this reply is streaming,
        // a slot may hold a newer line than it did at the snapshot — a log dump can
        // live with that, and it is a much better trade than the alternative.
        char line[MAX_LINE_LEN];
        {
            LOCK(mutex_);
            const int32_t idx = (start + i) % MAX_LINES;
            snprintf(line, sizeof(line), "%s", lines_[idx]);
        }
        lines.value(line);
    }
}   // `lines` closes here; caller's `resp` stays usable (auto-detached)

// ──────────────────────────────────────────────────────────────
// WebSocket commands
// ──────────────────────────────────────────────────────────────

RequestError ConsoleManager::Cmd_GetLogs(CommandContext& ctx)
{
    RETURN_IF_ERROR(ctx.readArgs());

    auto resp = ctx.reply.object();
    WriteHistory(resp);
    return RequestError::Ok;
}
