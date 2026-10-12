#include "pch.h"
#include "Core/Scripting.h"

namespace Kleio::Scripting
{
    namespace
    {
        bool EqualsAscii(std::wstring_view a, std::wstring_view b) noexcept
        {
            if (a.size() != b.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                wchar_t x = a[i];
                wchar_t y = b[i];
                if (x >= L'A' && x <= L'Z') x = static_cast<wchar_t>(x - L'A' + L'a');
                if (y >= L'A' && y <= L'Z') y = static_cast<wchar_t>(y - L'A' + L'a');
                if (x != y)
                {
                    return false;
                }
            }
            return true;
        }

        // "--get", "-get" and "/get" are all accepted (Windows tools use either style).
        std::optional<std::wstring_view> OptionName(std::wstring_view arg) noexcept
        {
            if (arg.starts_with(L"--"))
            {
                return arg.substr(2);
            }
            if (arg.size() > 1 && (arg.front() == L'-' || arg.front() == L'/'))
            {
                return arg.substr(1);
            }
            return std::nullopt;
        }

        std::optional<std::int32_t> ParseNumber(std::wstring_view text) noexcept
        {
            if (text.empty() || text.size() > 9)
            {
                return std::nullopt;
            }
            std::int32_t value = 0;
            for (wchar_t c : text)
            {
                if (c < L'0' || c > L'9')
                {
                    return std::nullopt;
                }
                value = value * 10 + (c - L'0');
            }
            return value;
        }

        bool LooksNumeric(std::wstring_view text) noexcept
        {
            if (text.starts_with(L'-') || text.starts_with(L'+'))
            {
                text.remove_prefix(1);
            }
            return !text.empty() && std::all_of(text.begin(), text.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
        }

        void AppendUtf8(std::vector<std::uint8_t>& bytes, std::wstring_view text)
        {
            const std::string utf8 = ToUtf8(text);
            bytes.insert(bytes.end(), utf8.begin(), utf8.end());
        }

        std::optional<std::size_t> HeaderValue(std::string_view header, std::string_view name)
        {
            const auto pos = header.find(name);
            if (pos == std::string_view::npos)
            {
                return std::nullopt;
            }
            std::size_t i = pos + name.size();
            while (i < header.size() && header[i] == ' ')
            {
                ++i;
            }
            std::size_t value = 0;
            bool any = false;
            while (i < header.size() && header[i] >= '0' && header[i] <= '9')
            {
                value = value * 10 + static_cast<std::size_t>(header[i] - '0');
                if (value > 0x7FFFFFFF)
                {
                    return std::nullopt;
                }
                any = true;
                ++i;
            }
            return any ? std::optional<std::size_t>{ value } : std::nullopt;
        }
    }

    std::wstring_view VerbName(Verb verb) noexcept
    {
        switch (verb)
        {
        case Verb::Get: return L"get";
        case Verb::Select: return L"select";
        case Verb::Delete: return L"delete";
        case Verb::Clear: return L"clear";
        case Verb::Help: return L"help";
        default: return L"";
        }
    }

    std::optional<Verb> VerbFromName(std::wstring_view name) noexcept
    {
        for (Verb verb : { Verb::Get, Verb::Select, Verb::Delete, Verb::Clear, Verb::Help })
        {
            if (EqualsAscii(name, VerbName(verb)))
            {
                return verb;
            }
        }
        return std::nullopt;
    }

    std::wstring_view FormatName(Format format) noexcept
    {
        switch (format)
        {
        case Format::Text: return L"text";
        case Format::Html: return L"html";
        case Format::Rtf: return L"rtf";
        case Format::Image: return L"image";
        case Format::Files: return L"files";
        default: return L"auto";
        }
    }

    std::optional<Format> FormatFromName(std::wstring_view name) noexcept
    {
        for (Format format : { Format::Auto, Format::Text, Format::Html, Format::Rtf, Format::Image, Format::Files })
        {
            if (EqualsAscii(name, FormatName(format)))
            {
                return format;
            }
        }
        if (EqualsAscii(name, L"png"))
        {
            return Format::Image;
        }
        if (EqualsAscii(name, L"file"))
        {
            return Format::Files;
        }
        return std::nullopt;
    }

    ParseResult Parse(std::vector<std::wstring> const& args)
    {
        // First pass: is this a scripting command line at all?
        std::optional<Verb> verb;
        for (auto const& arg : args)
        {
            if (auto name = OptionName(arg))
            {
                if (EqualsAscii(*name, L"h") || EqualsAscii(*name, L"?"))
                {
                    verb = verb.value_or(Verb::Help);
                }
                else if (auto found = VerbFromName(*name))
                {
                    if (verb && *verb != Verb::Help && *verb != *found)
                    {
                        return { std::nullopt, L"Use only one of --get, --select, --delete and --clear." };
                    }
                    if (!verb || *verb == Verb::Help)
                    {
                        verb = found;
                    }
                }
            }
        }
        if (!verb)
        {
            return {};
        }

        Command command;
        command.verb = *verb;
        if (command.verb == Verb::Help)
        {
            return { command, {} };
        }

        for (std::size_t i = 0; i < args.size(); ++i)
        {
            std::wstring_view const arg = args[i];
            auto name = LooksNumeric(arg) ? std::nullopt : OptionName(arg);
            if (!name)
            {
                // A bare number is the position for --get/--select/--delete.
                if (LooksNumeric(arg) && command.verb != Verb::Clear)
                {
                    if (command.number)
                    {
                        return { std::nullopt, L"More than one item number was given." };
                    }
                    auto number = ParseNumber(arg);
                    if (!number || *number < 1)
                    {
                        return { std::nullopt, L"The item number must be 1 or greater." };
                    }
                    command.number = number;
                    continue;
                }
                return { std::nullopt, L"Unexpected argument: " + std::wstring{ arg } };
            }

            // --name=value
            std::wstring_view key = *name;
            std::optional<std::wstring> inlineValue;
            if (auto equals = key.find(L'='); equals != std::wstring_view::npos)
            {
                inlineValue = std::wstring{ key.substr(equals + 1) };
                key = key.substr(0, equals);
            }
            auto takeValue = [&]() -> std::optional<std::wstring> {
                if (inlineValue)
                {
                    return inlineValue;
                }
                if (i + 1 < args.size())
                {
                    return args[++i];
                }
                return std::nullopt;
            };

            if (VerbFromName(key) && !inlineValue)
            {
                continue;
            }
            if (EqualsAscii(key, L"format") || EqualsAscii(key, L"f"))
            {
                if (command.verb != Verb::Get)
                {
                    return { std::nullopt, L"--format can only be used with --get." };
                }
                auto value = takeValue();
                auto format = value ? FormatFromName(*value) : std::nullopt;
                if (!format)
                {
                    return { std::nullopt, L"--format must be one of text, html, rtf, image, files." };
                }
                command.format = *format;
            }
            else if (EqualsAscii(key, L"output") || EqualsAscii(key, L"o"))
            {
                if (command.verb != Verb::Get)
                {
                    return { std::nullopt, L"--output can only be used with --get." };
                }
                auto value = takeValue();
                if (!value || value->empty())
                {
                    return { std::nullopt, L"--output needs a file path." };
                }
                command.output = *value;
            }
            else if (EqualsAscii(key, L"selected"))
            {
                if (command.verb != Verb::Get)
                {
                    return { std::nullopt, L"--selected can only be used with --get." };
                }
            }
            else if (EqualsAscii(key, L"yes") || EqualsAscii(key, L"y"))
            {
                if (command.verb != Verb::Clear)
                {
                    return { std::nullopt, L"--yes can only be used with --clear." };
                }
                command.yes = true;
            }
            else
            {
                return { std::nullopt, L"Unknown option: " + std::wstring{ arg } };
            }
        }

        // Maccy's intents default to the first item; Get defaults to the selected one.
        if (!command.number && (command.verb == Verb::Select || command.verb == Verb::Delete))
        {
            command.number = 1;
        }
        return { command, {} };
    }

    HistoryItemPtr ItemAt(std::vector<HistoryItemPtr> const& items, std::int32_t number)
    {
        if (number < 1 || static_cast<std::size_t>(number) > items.size())
        {
            return nullptr;
        }
        return items[static_cast<std::size_t>(number) - 1];
    }

    std::string HtmlFromCfHtml(std::string_view cfHtml)
    {
        const auto firstTag = cfHtml.find('<');
        const std::string_view header = cfHtml.substr(0, firstTag == std::string_view::npos ? cfHtml.size() : firstTag);
        const auto start = HeaderValue(header, "StartHTML:");
        const auto end = HeaderValue(header, "EndHTML:");
        if (start && end && *start <= *end && *end <= cfHtml.size())
        {
            return std::string{ cfHtml.substr(*start, *end - *start) };
        }
        if (start && *start < cfHtml.size())
        {
            return std::string{ cfHtml.substr(*start) };
        }
        if (header.starts_with("Version:") && firstTag != std::string_view::npos)
        {
            return std::string{ cfHtml.substr(firstTag) };
        }
        return std::string{ cfHtml };
    }

    std::optional<Payload> Extract(HistoryItem const& item, Format format)
    {
        Payload payload;
        payload.format = format;
        switch (format)
        {
        case Format::Auto:
            for (Format candidate : { Format::Text, Format::Files, Format::Image, Format::Html, Format::Rtf })
            {
                if (auto found = Extract(item, candidate))
                {
                    return found;
                }
            }
            return std::nullopt;
        case Format::Text:
            if (auto text = item.Text())
            {
                AppendUtf8(payload.bytes, *text);
                return payload;
            }
            return std::nullopt;
        case Format::Html:
            if (auto html = item.Html())
            {
                const std::string document = HtmlFromCfHtml(*html);
                payload.bytes.assign(document.begin(), document.end());
                return payload;
            }
            return std::nullopt;
        case Format::Rtf:
            if (auto rtf = item.Rtf())
            {
                payload.bytes.assign(rtf->begin(), rtf->end());
                return payload;
            }
            return std::nullopt;
        case Format::Image:
            if (auto const* png = item.Image())
            {
                payload.bytes = *png;
                return payload;
            }
            return std::nullopt;
        case Format::Files:
        {
            auto const files = item.Files();
            if (files.empty())
            {
                return std::nullopt;
            }
            std::wstring lines;
            for (auto const& file : files)
            {
                if (!lines.empty())
                {
                    lines += L"\r\n";
                }
                lines += file;
            }
            AppendUtf8(payload.bytes, lines);
            return payload;
        }
        }
        return std::nullopt;
    }

    std::string ToUtf8(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }
        const int size = static_cast<int>(text.size());
        const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), size, nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>((std::max)(length, 0)), '\0');
        if (length > 0)
        {
            WideCharToMultiByte(CP_UTF8, 0, text.data(), size, result.data(), length, nullptr, nullptr);
        }
        return result;
    }

    std::wstring FromUtf8(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }
        const int size = static_cast<int>(text.size());
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
        std::wstring result(static_cast<std::size_t>((std::max)(length, 0)), L'\0');
        if (length > 0)
        {
            MultiByteToWideChar(CP_UTF8, 0, text.data(), size, result.data(), length);
        }
        return result;
    }

    std::wstring Usage()
    {
        return LR"(Usage: kleio [command]

History (positions count from 1 and include pinned items, as listed in Kleio):
  --get [n] [--format f] [--output path]
                    Print the selected item, or the n-th item. Formats: text, html, rtf,
                    image, files (one path per line). Without --format the first available
                    of text, files, image, html, rtf is used. Images are printed as base64
                    unless --output is given; --output writes the content to a file and
                    prints its path.
  --select [n]      Copy the n-th item (default 1), like choosing it in Kleio. Pastes too
                    when "Paste automatically" is on. Prints the item's title.
  --delete [n]      Delete the n-th item (default 1).
  --clear [--yes]   Clear the history except pinned items. Asks first unless --yes is given
                    or "Don't ask again" was chosen in Kleio.

Recording:
  --pause           Ignore all new copies.
  --resume          Record copies again.
  --ignore-next     Ignore only the next copy.

App:
  --settings        Open Settings.
  --quit            Quit Kleio.
  --help            Show this help.

Exit codes: 0 success, 1 item or format not found, 2 invalid arguments, 3 Kleio not
reachable, 4 cancelled.
)";
    }
}
