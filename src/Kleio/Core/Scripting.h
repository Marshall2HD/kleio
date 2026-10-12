#pragma once

#include "HistoryItem.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Command-line scripting verbs: the Windows counterpart of Maccy's App Intents (Maccy/Intents:
// Get, Select, Delete, Clear). Parsing and item/content resolution live here so they can be unit
// tested; Platform/ScriptingPipe carries them to the running instance and back.
namespace Kleio::Scripting
{
    enum class Verb : std::int32_t { None = 0, Get, Select, Delete, Clear, Help };
    enum class Format : std::int32_t { Auto = 0, Text, Html, Rtf, Image, Files };

    // Process exit codes of the scripting verbs.
    enum ExitCode : int
    {
        Success = 0,
        NotFound = 1,        // no item at that position / nothing selected / the item lacks the format
        UsageError = 2,      // invalid arguments
        Unavailable = 3,     // could not reach (or start) Kleio
        Cancelled = 4,       // the user declined the Clear confirmation
    };

    struct Command
    {
        Verb verb = Verb::None;
        // 1-based position in the history list (pins included, in their PinTo position), as in
        // Maccy's intents. nullopt for Get = the item selected in the popup (Maccy's "Selected").
        std::optional<std::int32_t> number;
        Format format = Format::Auto;      // Get only
        std::wstring output;               // Get only: write the content to this file
        bool yes = false;                  // Clear only: skip the confirmation
    };

    struct ParseResult
    {
        std::optional<Command> command;    // nullopt: not a scripting command line (or an error)
        std::wstring error;                // set when a scripting verb was found but is malformed
    };

    // Parses the arguments after the executable name. Returns an empty result (no command, no
    // error) when no scripting verb is present, so the caller can fall back to the other
    // activation flags (--pause, --settings, ...).
    ParseResult Parse(std::vector<std::wstring> const& args);

    std::wstring_view VerbName(Verb verb) noexcept;          // "get", ...
    std::optional<Verb> VerbFromName(std::wstring_view name) noexcept;
    std::wstring_view FormatName(Format format) noexcept;    // "auto", "text", ...
    std::optional<Format> FormatFromName(std::wstring_view name) noexcept;

    // Maccy: `items[number - 1]` over History.items (pinned + unpinned in PinTo order). nullptr
    // when `number` is out of range (Maccy's AppIntentError.notFound).
    HistoryItemPtr ItemAt(std::vector<HistoryItemPtr> const& items, std::int32_t number);

    struct Payload
    {
        Format format = Format::Text;      // the format actually returned (never Auto)
        std::vector<std::uint8_t> bytes;   // UTF-8 for Text/Html/Files, raw RTF, PNG for Image
    };

    // Content of `item` in `format`. Auto picks text, then files, then image, then HTML, then RTF
    // (the first property of Maccy's HistoryItemAppEntity that the item has). nullopt when the item
    // has no such content.
    std::optional<Payload> Extract(HistoryItem const& item, Format format);

    // Outcome of a command, computed by the running instance and printed by the calling process.
    struct Result
    {
        int exitCode = Success;
        std::wstring error;                // printed to stderr when exitCode != Success
        Payload payload;                   // Get: the content; Select: the item title (Text)
    };

    // The HTML document of a CF_HTML payload (StartHTML..EndHTML; the header is dropped).
    std::string HtmlFromCfHtml(std::string_view cfHtml);

    std::string ToUtf8(std::wstring_view text);
    std::wstring FromUtf8(std::string_view text);

    // `kleio --help` text.
    std::wstring Usage();
}
