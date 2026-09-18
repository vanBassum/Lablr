#include "CommandManager.h"
#include "JsonArgReader.h"
#include "JsonReplyWriter.h"
#include "DescribeArgReader.h"
#include "Stream.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

CommandManager::CommandManager(StruxProvider& strux)
    : strux_(strux)
{
}

void CommandManager::Init()
{
    auto initAttempt = initState_.TryBeginInit();
    if (!initAttempt)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    Register(this, commands_);

    initAttempt.SetReady();
    ESP_LOGI(TAG, "Initialized");
}

RequestError CommandManager::Execute(const char* category, const char* name,
                                     Stream& in, Stream& out,
                                     ConnectionAuth* connection,
                                     const char** failedArg)
{
    const CommandEntry* e = Find(category, name);
    if (e == nullptr)
        return RequestError::UnknownCommand;

    // Handler runs OUTSIDE the lock: entries are immortal, so the pointer
    // stays valid, and a handler may register commands or dispatch nested
    // commands without deadlocking.
    // Parse the arguments first, so the handler receives them already validated and
    // `in` already positioned at the body. JsonArgs is the only thing in the request
    // path holding a request-sized buffer — swapping in a token implementation here
    // deletes it without touching a single handler.
    JsonArgReader reader(in);
    JsonReplyWriter writer(out);
    CommandContext ctx(reader, writer, in, out, connection);
    const RequestError err = e->handler(e->ctx, ctx);
    if (err != RequestError::Ok && failedArg)
        *failedArg = reader.failedArgument();
    return err;
}

const char* DescribeRequestError(RequestError e, const char* arg, char* buf, size_t cap)
{
    switch (e)   // no default: a new RequestError must be handled here
    {
    case RequestError::Ok:              return "ok";
    case RequestError::UnknownCommand:  return "unknown command";
    case RequestError::MissingArgument:
        snprintf(buf, cap, "missing required argument: %s", arg ? arg : "?");
        return buf;
    case RequestError::MalformedRequest:  return "malformed request";
    case RequestError::MalformedNumber:
        snprintf(buf, cap, "malformed number: %s", arg ? arg : "?");
        return buf;
    case RequestError::ArgumentTooLong:
        snprintf(buf, cap, "argument too long: %s", arg ? arg : "?");
        return buf;
    case RequestError::Described:  return "described";   // help swallows this
    }
    return "bad request";
}

// ──────────────────────────────────────────────────────────────
// help
// ──────────────────────────────────────────────────────────────

namespace {

// The streams the described handler gets. It is stopped at its readArgs call, so
// these exist to be unused — and to mean that a handler which somehow reaches its
// body writes to nobody instead of to the client.
class NullStream final : public Stream
{
public:
    size_t write(const void*, size_t size, TickType_t = portMAX_DELAY) override { return size; }
    size_t read(void*, size_t, TickType_t = portMAX_DELAY) override { return 0; }
};

} // namespace

RequestError CommandManager::Cmd_Help(CommandContext& ctx)
{
    char category[MAX_ROUTE] = {};
    char command[MAX_ROUTE]  = {};

    RETURN_IF_ERROR(ctx.readArgs(
        Optional("category", category,
                 "Limit the answer to one category, e.g. 'partition'. Omit it to list "
                 "every category and its commands."),
        Optional("command",  command,
                 "Describe this one command's arguments. Needs 'category' as well, "
                 "because a route is two words.")
    ));

    if (command[0] != '\0')
        return DescribeCommand(category, command, ctx.reply);

    if (category[0] != '\0')
        ListCategory(category, ctx.reply);
    else
        ListCategories(ctx.reply);

    return RequestError::Ok;
}

size_t CommandManager::CollectCategories(const char** out, size_t cap, bool& truncated)
{
    // The chain has no notion of a category, so the distinct ones are collected by
    // walking it. Fixed array, and it says so when it fills up rather than quietly
    // answering with part of the registry.
    size_t count = 0;
    truncated = false;

    for (const CommandEntry* e = head_; e != nullptr; e = e->next)
    {
        bool known = false;
        for (size_t i = 0; i < count && !known; ++i)
            known = strcmp(out[i], e->category) == 0;
        if (known) continue;

        if (count == cap) { truncated = true; break; }
        out[count++] = e->category;
    }

    return count;
}

void CommandManager::ListCategories(ReplyWriter& reply)
{
    // Held across the JSON, so a manager registering from another task cannot relink
    // the chain half way through the answer. Safe to write to `out` from under it:
    // nothing acquires this mutex from inside a transport's send path, so there is no
    // pair of locks to take in two orders.
    LOCK(mutex_);

    const char* seen[MAX_CATEGORIES];
    bool truncated = false;
    const size_t count = CollectCategories(seen, MAX_CATEGORIES, truncated);

    auto resp = reply.object();
    resp.field("ok", true);
    {
        auto cats = resp.array("categories");
        for (size_t i = 0; i < count; ++i)
        {
            auto cat = cats.object();
            cat.field("category", seen[i]);
            auto names = cat.array("commands");
            for (const CommandEntry* e = head_; e != nullptr; e = e->next)
                if (strcmp(seen[i], e->category) == 0)
                    names.value(e->name);
        }
    }
    if (truncated)
        resp.field("truncated", true);
}

void CommandManager::ListCategory(const char* category, ReplyWriter& reply)
{
    LOCK(mutex_);   // see ListCategories

    auto resp = reply.object();

    bool found = false;
    for (const CommandEntry* e = head_; e != nullptr && !found; e = e->next)
        found = strcmp(category, e->category) == 0;

    if (!found)
    {
        resp.field("ok", false);
        resp.field("error", "unknown category");
        return;
    }

    resp.field("ok", true);
    resp.field("category", category);
    auto names = resp.array("commands");
    for (const CommandEntry* e = head_; e != nullptr; e = e->next)
        if (strcmp(category, e->category) == 0)
            names.value(e->name);
}

RequestError CommandManager::DescribeCommand(const char* category, const char* command,
                                             ReplyWriter& reply)
{
    auto resp = reply.object();

    if (category[0] == '\0')
    {
        // Routes are two words all the way down, so a command without its category
        // is not a route. Meaning rather than form, so it goes in the reply.
        resp.field("ok", false);
        resp.field("error", "a command needs its category");
        return RequestError::Ok;
    }

    const CommandEntry* e = Find(category, command);
    if (e == nullptr)
    {
        resp.field("ok", false);
        resp.field("error", "unknown command");
        return RequestError::Ok;
    }

    resp.field("ok", true);
    resp.field("category", e->category);
    resp.field("command", e->name);
    if (e->help != nullptr && e->help[0] != '\0')
        resp.field("description", e->help);

    auto args = resp.array("arguments");
    if (!DescribeArguments(*e, args))
        resp.field("declared", false);   // writing to the parent closes `args`

    return RequestError::Ok;
}

bool CommandManager::DescribeArguments(const CommandEntry& entry, ReplyArray& args)
{
    // Not through Execute(): that one is the wire path — it builds the reader for
    // today's format and reports which argument a parse tripped over. Here the reader
    // IS the point, and there is no request to parse.
    DescribeArgReader reader(args);
    NullStream sink;
    JsonReplyWriter nowhere(sink);   // the described handler is stopped before it replies
    CommandContext described(reader, nowhere, sink, sink, nullptr);

    if (entry.handler(entry.ctx, described) == RequestError::Described)
        return true;

    // The handler returned without ever asking for its arguments, which means it ran
    // its body — under help, against streams that go nowhere. Nothing here can undo
    // that; the fix is a readArgs call in the handler.
    ESP_LOGE(TAG, "'%s %s' declares no arguments - its body ran under help",
             entry.category, entry.name);
    return false;
}

RequestError CommandManager::Cmd_Describe(CommandContext& ctx)
{
    char category[MAX_ROUTE] = {};

    RETURN_IF_ERROR(ctx.readArgs(
        Optional("category", category,
                 "Describe only this category's commands. Omit it for the whole "
                 "registry, which is the usual call.")
    ));

    // Held across the whole reply, for the same reason ListCategories holds it — and
    // with one addition: every handler on the chain is re-dispatched from in here.
    // That is safe because a described handler is stopped at its own readArgs before
    // its body runs, so it registers nothing and dispatches nothing; the mutex is
    // recursive anyway, so a handler that did would not deadlock.
    LOCK(mutex_);

    const char* seen[MAX_CATEGORIES];
    bool truncated = false;
    const size_t count = CollectCategories(seen, MAX_CATEGORIES, truncated);

    auto resp = ctx.reply.object();
    resp.field("ok", true);
    {
        auto cats = resp.array("categories");
        for (size_t i = 0; i < count; ++i)
        {
            if (category[0] != '\0' && strcmp(category, seen[i]) != 0)
                continue;

            auto cat = cats.object();
            cat.field("category", seen[i]);

            auto commands = cat.array("commands");
            for (const CommandEntry* e = head_; e != nullptr; e = e->next)
            {
                if (strcmp(seen[i], e->category) != 0) continue;

                auto cmd = commands.object();
                cmd.field("name", e->name);
                if (e->help != nullptr && e->help[0] != '\0')
                    cmd.field("description", e->help);

                auto args = cmd.array("arguments");
                if (!DescribeArguments(*e, args))
                {
                    // An undeclared handler wrote nothing to the array, and an empty
                    // array reads as "takes no arguments" — which is a lie a caller
                    // would act on. Writing to the parent closes `args` and says so.
                    cmd.field("declared", false);
                }
            }
        }
    }
    if (truncated)
        resp.field("truncated", true);

    return RequestError::Ok;
}

const CommandEntry* CommandManager::Find(const char* category, const char* name)
{
    LOCK(mutex_);
    for (CommandEntry* e = head_; e != nullptr; e = e->next)
        if (strcmp(name, e->name) == 0 && strcmp(category, e->category) == 0)
            return e;
    return nullptr;
}
