#include "pch.h"
#include "Core/HistoryItem.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>

namespace Kleio
{
    namespace
    {
        constexpr wchar_t kMiddleDot = L'\u00B7';        // '·' leading/trailing spaces
        constexpr wchar_t kReturnSymbol = L'\u23CE';     // '⏎' newlines
        constexpr wchar_t kTabSymbol = L'\u21E5';        // '⇥' tabs
        constexpr wchar_t kObjectReplacement = L'\uFFFC';   // OBJECT REPLACEMENT CHARACTER
        constexpr std::size_t kTitleMaxLength = 1000;

        // Registered clipboard format names are case-insensitive.
        bool EqualsIgnoreAsciiCase(std::wstring_view a, std::wstring_view b) noexcept
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

        bool IsHighSurrogate(wchar_t c) noexcept
        {
            return static_cast<std::uint32_t>(c) >= 0xD800 && static_cast<std::uint32_t>(c) <= 0xDBFF;
        }

        void AppendCodePoint(std::wstring& out, std::uint32_t cp)
        {
            if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            {
                cp = 0xFFFD;
            }
            if constexpr (sizeof(wchar_t) == 2)
            {
                if (cp >= 0x10000)
                {
                    cp -= 0x10000;
                    out += static_cast<wchar_t>(0xD800 + (cp >> 10));
                    out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
                    return;
                }
            }
            out += static_cast<wchar_t>(cp);
        }

        // Swift's String.shortened(to:) counts Characters; we count code points so that surrogate
        // pairs are never split.
        std::wstring Shortened(std::wstring const& text, std::size_t maxLength)
        {
            std::size_t count = 0;
            std::size_t i = 0;
            while (i < text.size())
            {
                if (count == maxLength)
                {
                    return text.substr(0, i);
                }
                if (IsHighSurrogate(text[i]) && i + 1 < text.size())
                {
                    i += 2;
                }
                else
                {
                    i += 1;
                }
                ++count;
            }
            return text;
        }

        // Unicode White_Space (Foundation's CharacterSet.whitespacesAndNewlines).
        bool IsWhitespaceOrNewline(wchar_t c) noexcept
        {
            switch (static_cast<std::uint32_t>(c))
            {
            case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20:
            case 0x85: case 0xA0: case 0x1680:
            case 0x2000: case 0x2001: case 0x2002: case 0x2003: case 0x2004: case 0x2005:
            case 0x2006: case 0x2007: case 0x2008: case 0x2009: case 0x200A:
            case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000:
                return true;
            default:
                return false;
            }
        }

        bool IsLineTerminator(wchar_t c) noexcept
        {
            const auto u = static_cast<std::uint32_t>(c);
            return u == 0x0A || u == 0x0D || u == 0x85 || u == 0x2028 || u == 0x2029 || u == 0x0B || u == 0x0C;
        }

        std::wstring TrimWhitespaceAndNewlines(std::wstring const& text)
        {
            std::size_t start = 0;
            std::size_t end = text.size();
            while (start < end && IsWhitespaceOrNewline(text[start]))
            {
                ++start;
            }
            while (end > start && IsWhitespaceOrNewline(text[end - 1]))
            {
                --end;
            }
            return text.substr(start, end - start);
        }

        std::wstring DecodeCodePage(std::string_view bytes, unsigned int codePage)
        {
            if (bytes.empty())
            {
                return {};
            }
            const int size = static_cast<int>(std::min<std::size_t>(bytes.size(), 0x7FFFFFFF));
            const int length = MultiByteToWideChar(codePage, 0, bytes.data(), size, nullptr, 0);
            if (length > 0)
            {
                std::wstring result(static_cast<std::size_t>(length), L'\0');
                const int written = MultiByteToWideChar(codePage, 0, bytes.data(), size, result.data(), length);
                if (written > 0)
                {
                    result.resize(static_cast<std::size_t>(written));
                    return result;
                }
            }
            // Unknown code page: Latin-1 is the best deterministic guess.
            std::wstring result;
            result.reserve(bytes.size());
            for (char c : bytes)
            {
                result += static_cast<wchar_t>(static_cast<unsigned char>(c));
            }
            return result;
        }

        constexpr unsigned int kCodePageUtf8 = 65001;
        constexpr unsigned int kCodePageWindows1252 = 1252;

        int HexDigit(int c) noexcept
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        bool IsAsciiLetter(int c) noexcept
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }

        bool IsAsciiDigit(int c) noexcept
        {
            return c >= '0' && c <= '9';
        }

        // ---------------------------------------------------------------- RTF

        bool IsSkippedRtfDestination(std::string_view word) noexcept
        {
            static constexpr std::string_view kDestinations[] = {
                "fonttbl", "colortbl", "stylesheet", "info", "pict", "objdata", "header", "headerl",
                "headerr", "headerf", "footer", "footerl", "footerr", "footerf", "listtable",
                "listoverridetable", "revtbl", "rsidtbl", "xmlnstbl", "themedata", "colorschememapping",
                "datastore", "latentstyles", "generator", "fldinst", "filetbl", "pgdsctbl", "mmathPr",
                "author", "operator", "title", "subject", "keywords", "comment", "doccomm", "company",
                "nonshppict", "bkmkstart", "bkmkend", "private", "expandedcolortbl", "listpicture",
                "falt", "panose", "fontemb", "fontfile", "stylesheet", "xe", "tc", "txe", "atnid",
                "atnauthor", "annotation", "footnote", "shppict", "blipuid", "passwordhash", "wgrffmtfilter",
            };
            for (auto destination : kDestinations)
            {
                if (word == destination)
                {
                    return true;
                }
            }
            return false;
        }

        // ---------------------------------------------------------------- HTML

        std::wstring AsciiLowerW(std::wstring_view text)
        {
            std::wstring result(text);
            for (auto& c : result)
            {
                if (c >= L'A' && c <= L'Z')
                {
                    c = static_cast<wchar_t>(c - L'A' + L'a');
                }
            }
            return result;
        }

        bool IsHtmlBlockTag(std::wstring_view name) noexcept
        {
            static constexpr std::wstring_view kBlocks[] = {
                L"p", L"div", L"li", L"ul", L"ol", L"tr", L"table", L"h1", L"h2", L"h3", L"h4", L"h5", L"h6",
                L"blockquote", L"pre", L"section", L"article", L"header", L"footer", L"nav", L"aside", L"dl",
                L"dt", L"dd", L"hr", L"address", L"figure", L"figcaption", L"main", L"form", L"fieldset",
                L"center", L"tbody", L"thead", L"tfoot", L"caption", L"details", L"summary",
            };
            for (auto block : kBlocks)
            {
                if (name == block)
                {
                    return true;
                }
            }
            return false;
        }

        std::optional<std::uint32_t> NamedEntity(std::wstring_view name)
        {
            struct Entity
            {
                std::wstring_view name;
                std::uint32_t codePoint;
            };
            static constexpr Entity kEntities[] = {
                { L"amp", '&' }, { L"lt", '<' }, { L"gt", '>' }, { L"quot", '"' }, { L"apos", '\'' },
                { L"nbsp", ' ' }, { L"ensp", 0x2002 }, { L"emsp", 0x2003 }, { L"thinsp", 0x2009 },
                { L"copy", 0xA9 }, { L"reg", 0xAE }, { L"trade", 0x2122 }, { L"hellip", 0x2026 },
                { L"mdash", 0x2014 }, { L"ndash", 0x2013 }, { L"lsquo", 0x2018 }, { L"rsquo", 0x2019 },
                { L"sbquo", 0x201A }, { L"ldquo", 0x201C }, { L"rdquo", 0x201D }, { L"bdquo", 0x201E },
                { L"laquo", 0xAB }, { L"raquo", 0xBB }, { L"bull", 0x2022 }, { L"middot", 0xB7 },
                { L"euro", 0x20AC }, { L"pound", 0xA3 }, { L"yen", 0xA5 }, { L"cent", 0xA2 },
                { L"sect", 0xA7 }, { L"para", 0xB6 }, { L"deg", 0xB0 }, { L"plusmn", 0xB1 },
                { L"times", 0xD7 }, { L"divide", 0xF7 }, { L"frac12", 0xBD }, { L"frac14", 0xBC },
                { L"frac34", 0xBE }, { L"iexcl", 0xA1 }, { L"iquest", 0xBF }, { L"shy", 0xAD },
                { L"auml", 0xE4 }, { L"ouml", 0xF6 }, { L"uuml", 0xFC }, { L"Auml", 0xC4 },
                { L"Ouml", 0xD6 }, { L"Uuml", 0xDC }, { L"szlig", 0xDF }, { L"eacute", 0xE9 },
                { L"egrave", 0xE8 }, { L"aacute", 0xE1 }, { L"agrave", 0xE0 }, { L"ccedil", 0xE7 },
                { L"ntilde", 0xF1 }, { L"oacute", 0xF3 }, { L"iacute", 0xED }, { L"uacute", 0xFA },
                { L"Eacute", 0xC9 }, { L"rarr", 0x2192 }, { L"larr", 0x2190 }, { L"uarr", 0x2191 },
                { L"darr", 0x2193 }, { L"zwj", 0x200D }, { L"zwnj", 0x200C },
            };
            for (auto const& entity : kEntities)
            {
                if (entity.name == name)
                {
                    return entity.codePoint;
                }
            }
            return std::nullopt;
        }

        std::optional<std::size_t> CfHtmlHeaderValue(std::string_view header, std::string_view name)
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
            if (i >= header.size() || !IsAsciiDigit(header[i]))
            {
                return std::nullopt;
            }
            std::size_t value = 0;
            while (i < header.size() && IsAsciiDigit(header[i]))
            {
                value = value * 10 + static_cast<std::size_t>(header[i] - '0');
                if (value > 0x7FFFFFFF)
                {
                    return std::nullopt;
                }
                ++i;
            }
            return value;
        }

        std::string_view CfHtmlFragment(std::string_view cfHtml)
        {
            const auto firstTag = cfHtml.find('<');
            const std::string_view header = cfHtml.substr(0, firstTag == std::string_view::npos ? cfHtml.size() : firstTag);

            const auto startFragment = CfHtmlHeaderValue(header, "StartFragment:");
            const auto endFragment = CfHtmlHeaderValue(header, "EndFragment:");
            if (startFragment && endFragment && *startFragment <= *endFragment && *endFragment <= cfHtml.size())
            {
                return cfHtml.substr(*startFragment, *endFragment - *startFragment);
            }

            constexpr std::string_view kStartMarker = "<!--StartFragment-->";
            constexpr std::string_view kEndMarker = "<!--EndFragment-->";
            const auto startMarker = cfHtml.find(kStartMarker);
            if (startMarker != std::string_view::npos)
            {
                const auto fragmentStart = startMarker + kStartMarker.size();
                const auto endMarker = cfHtml.find(kEndMarker, fragmentStart);
                if (endMarker != std::string_view::npos)
                {
                    return cfHtml.substr(fragmentStart, endMarker - fragmentStart);
                }
            }

            const auto startHtml = CfHtmlHeaderValue(header, "StartHTML:");
            if (startHtml && *startHtml < cfHtml.size())
            {
                return cfHtml.substr(*startHtml);
            }
            if (header.starts_with("Version:") && firstTag != std::string_view::npos)
            {
                return cfHtml.substr(firstTag);
            }
            return cfHtml;
        }

        std::size_t FindIgnoreAsciiCase(std::wstring_view haystack, std::wstring_view needle, std::size_t from)
        {
            if (needle.empty())
            {
                return from;
            }
            for (std::size_t i = from; i + needle.size() <= haystack.size(); ++i)
            {
                bool match = true;
                for (std::size_t j = 0; j < needle.size(); ++j)
                {
                    wchar_t a = haystack[i + j];
                    wchar_t b = needle[j];
                    if (a >= L'A' && a <= L'Z') a = static_cast<wchar_t>(a - L'A' + L'a');
                    if (b >= L'A' && b <= L'Z') b = static_cast<wchar_t>(b - L'A' + L'a');
                    if (a != b)
                    {
                        match = false;
                        break;
                    }
                }
                if (match)
                {
                    return i;
                }
            }
            return std::wstring_view::npos;
        }

        // Swift's Int(hex, radix: 16) for an ASCII hex string (no sign handling needed here).
        bool ParseHex(std::wstring_view text, std::uint32_t& value) noexcept
        {
            if (text.empty() || text.size() > 8)
            {
                return false;
            }
            std::uint32_t result = 0;
            for (wchar_t c : text)
            {
                const int digit = (static_cast<std::uint32_t>(c) < 0x80) ? HexDigit(static_cast<int>(c)) : -1;
                if (digit < 0)
                {
                    return false;
                }
                result = (result << 4) | static_cast<std::uint32_t>(digit);
            }
            value = result;
            return true;
        }
    }

    Timestamp Now() noexcept
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    // ------------------------------------------------------------------ encodings

    std::vector<std::uint8_t> EncodeText(std::wstring_view text)
    {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(text.size() * 2);
        auto push = [&bytes](std::uint32_t unit) {
            bytes.push_back(static_cast<std::uint8_t>(unit & 0xFF));
            bytes.push_back(static_cast<std::uint8_t>((unit >> 8) & 0xFF));
        };
        for (wchar_t c : text)
        {
            auto unit = static_cast<std::uint32_t>(c);
            if (unit >= 0x10000)
            {
                // Only reachable where wchar_t is UTF-32.
                unit -= 0x10000;
                push(0xD800 + (unit >> 10));
                push(0xDC00 + (unit & 0x3FF));
            }
            else
            {
                push(unit);
            }
        }
        return bytes;
    }

    std::wstring DecodeText(std::vector<std::uint8_t> const& bytes)
    {
        std::wstring text;
        text.reserve(bytes.size() / 2);
        for (std::size_t i = 0; i + 1 < bytes.size(); i += 2)
        {
            const auto unit = static_cast<std::uint32_t>(bytes[i]) | (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
            text += static_cast<wchar_t>(unit);
        }
        while (!text.empty() && text.back() == L'\0')
        {
            text.pop_back();
        }
        return text;
    }

    std::vector<std::uint8_t> EncodeFiles(std::vector<std::wstring> const& paths)
    {
        std::wstring joined;
        for (std::size_t i = 0; i < paths.size(); ++i)
        {
            if (i > 0)
            {
                joined += L'\n';
            }
            joined += paths[i];
        }
        return EncodeText(joined);
    }

    // ------------------------------------------------------------------ HistoryItem

    bool IsTransientType(std::wstring_view type) noexcept
    {
        // Maccy's HistoryItem.transientTypes with their Windows counterparts.
        static constexpr std::wstring_view kTransientTypes[] = {
            ContentType::Modified,                      // x.nspasteboard.ModifiedType
            ContentType::FromKleio,                     // org.p0deje.Maccy
            ContentType::CanIncludeInClipboardHistory,  // clipboard policy markers (Windows)
            ContentType::CanUploadToCloudClipboard,
            L"Chromium internal source URL",            // org.chromium.source-url
            L"Chromium Web Custom MIME Data Format",    // org.chromium.web-custom-data
            L"text/x-moz-url-priv",                     // Firefox's source page URL
        };
        if (type.size() >= 6 && EqualsIgnoreAsciiCase(type.substr(0, 6), L"Kleio."))
        {
            return true;
        }
        for (auto transient : kTransientTypes)
        {
            if (EqualsIgnoreAsciiCase(type, transient))
            {
                return true;
            }
        }
        return false;
    }

    bool HistoryItem::Supersedes(HistoryItem const& other) const
    {
        for (auto const& content : other.contents)
        {
            if (IsTransientType(content.type))
            {
                continue;
            }
            const bool found = std::any_of(contents.begin(), contents.end(), [&content](HistoryItemContent const& mine) {
                return mine.type == content.type && mine.value == content.value;
            });
            if (!found)
            {
                return false;
            }
        }
        return true;
    }

    HistoryItemContent const* HistoryItem::Find(std::wstring_view type) const noexcept
    {
        for (auto const& content : contents)
        {
            if (content.type == type)
            {
                return &content;
            }
        }
        return nullptr;
    }

    std::optional<std::uint32_t> HistoryItem::Modified() const
    {
        // Swift's Int(String(data:encoding: .utf8)): optional sign, decimal digits, nothing else.
        // A terminating NUL (usual for Windows text formats) is tolerated.
        auto content = Find(ContentType::Modified);
        if (!content)
        {
            return std::nullopt;
        }
        std::string_view text{ reinterpret_cast<char const*>(content->value.data()), content->value.size() };
        while (!text.empty() && text.back() == '\0')
        {
            text.remove_suffix(1);
        }
        if (!text.empty() && text.front() == '+')
        {
            text.remove_prefix(1);
        }
        if (text.empty() || text.size() > 10)
        {
            return std::nullopt;
        }
        std::uint64_t value = 0;
        for (char c : text)
        {
            if (!IsAsciiDigit(c))
            {
                return std::nullopt;
            }
            value = value * 10 + static_cast<std::uint64_t>(c - '0');
        }
        if (value > 0xFFFFFFFFull)
        {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(value);
    }

    std::optional<std::wstring> HistoryItem::Text() const
    {
        if (auto content = Find(ContentType::Text))
        {
            return DecodeText(content->value);
        }
        return std::nullopt;
    }

    std::optional<std::string> HistoryItem::Html() const
    {
        if (auto content = Find(ContentType::Html))
        {
            std::string html(content->value.begin(), content->value.end());
            while (!html.empty() && html.back() == '\0')
            {
                html.pop_back();
            }
            return html;
        }
        return std::nullopt;
    }

    std::optional<std::string> HistoryItem::Rtf() const
    {
        if (auto content = Find(ContentType::Rtf))
        {
            std::string rtf(content->value.begin(), content->value.end());
            while (!rtf.empty() && rtf.back() == '\0')
            {
                rtf.pop_back();
            }
            return rtf;
        }
        return std::nullopt;
    }

    std::vector<std::wstring> HistoryItem::Files() const
    {
        std::vector<std::wstring> files;
        auto content = Find(ContentType::Files);
        if (!content)
        {
            return files;
        }
        const std::wstring joined = DecodeText(content->value);
        std::size_t start = 0;
        while (start <= joined.size())
        {
            auto end = joined.find(L'\n', start);
            if (end == std::wstring::npos)
            {
                end = joined.size();
            }
            std::wstring path = joined.substr(start, end - start);
            if (!path.empty() && path.back() == L'\r')
            {
                path.pop_back();
            }
            if (!path.empty())
            {
                files.push_back(std::move(path));
            }
            start = end + 1;
        }
        return files;
    }

    std::vector<std::uint8_t> const* HistoryItem::Image() const
    {
        if (auto content = Find(ContentType::Png))
        {
            return &content->value;
        }
        return nullptr;
    }

    std::wstring HistoryItem::PreviewableText() const
    {
        const auto files = Files();
        if (!files.empty())
        {
            std::wstring joined;
            for (std::size_t i = 0; i < files.size(); ++i)
            {
                if (i > 0)
                {
                    joined += L'\n';
                }
                joined += files[i];
            }
            return joined;
        }
        if (auto text = Text(); text && !text->empty())
        {
            return *text;
        }
        if (auto rtf = Rtf())
        {
            auto plain = PlainTextFromRtf(*rtf);
            if (!plain.empty())
            {
                return plain;
            }
        }
        if (auto html = Html())
        {
            auto plain = PlainTextFromHtml(*html);
            if (!plain.empty())
            {
                return plain;
            }
        }
        return title;
    }

    std::wstring HistoryItem::GenerateTitle(bool showSpecialSymbols) const
    {
        if (IsImage())
        {
            return {};   // filled in asynchronously by OCR
        }

        // 1k characters is trade-off for performance
        std::wstring source = Shortened(PreviewableText(), kTitleMaxLength);
        std::erase(source, kObjectReplacement);

        if (!showSpecialSymbols)
        {
            return TrimWhitespaceAndNewlines(source);
        }

        // "^ +" -> '·'
        std::size_t leading = 0;
        while (leading < source.size() && source[leading] == L' ')
        {
            source[leading] = kMiddleDot;
            ++leading;
        }

        // " +$" -> '·'. Like ICU's '$' (without multiline) this also matches right before a line
        // terminator that ends the input.
        std::size_t end = source.size();
        if (end >= 2 && source[end - 2] == L'\r' && source[end - 1] == L'\n')
        {
            end -= 2;
        }
        else if (end >= 1 && source[end - 1] != L' ' && IsLineTerminator(source[end - 1]))
        {
            end -= 1;
        }
        if (end == source.size() || (end > 0 && source[end - 1] == L' '))
        {
            std::size_t i = end;
            while (i > 0 && source[i - 1] == L' ')
            {
                source[i - 1] = kMiddleDot;
                --i;
            }
        }

        // Newlines (CRLF, LF or CR) -> '⏎', tabs -> '⇥'.
        std::wstring result;
        result.reserve(source.size());
        for (std::size_t i = 0; i < source.size(); ++i)
        {
            const wchar_t c = source[i];
            if (c == L'\r')
            {
                if (i + 1 < source.size() && source[i + 1] == L'\n')
                {
                    ++i;
                }
                result += kReturnSymbol;
            }
            else if (c == L'\n')
            {
                result += kReturnSymbol;
            }
            else if (c == L'\t')
            {
                result += kTabSymbol;
            }
            else
            {
                result += c;
            }
        }
        return result;
    }

    std::optional<std::uint32_t> HistoryItem::HexColor() const
    {
        // Mirrors SwiftHEXColors' NSColor(hexString:) as used by Maccy's ColorImage.from(title):
        // optional leading '#', then exactly 3 (RGB), 4 (RGBA), 6 (RRGGBB) or 8 (RRGGBBAA) hex digits.
        std::wstring_view hex = title;
        std::optional<std::wstring> text;
        if (hex.empty())
        {
            text = Text();
            if (!text)
            {
                return std::nullopt;
            }
            hex = *text;
        }
        if (!hex.empty() && hex.front() == L'#')
        {
            hex.remove_prefix(1);
        }
        std::uint32_t value = 0;
        if (!ParseHex(hex, value))
        {
            return std::nullopt;
        }
        std::uint32_t r = 0, g = 0, b = 0, a = 0xFF;
        switch (hex.size())
        {
        case 3:
            r = ((value >> 8) & 0xF) * 0x11;
            g = ((value >> 4) & 0xF) * 0x11;
            b = (value & 0xF) * 0x11;
            break;
        case 4:
            r = ((value >> 12) & 0xF) * 0x11;
            g = ((value >> 8) & 0xF) * 0x11;
            b = ((value >> 4) & 0xF) * 0x11;
            a = (value & 0xF) * 0x11;
            break;
        case 6:
            r = (value >> 16) & 0xFF;
            g = (value >> 8) & 0xFF;
            b = value & 0xFF;
            break;
        case 8:
            r = (value >> 24) & 0xFF;
            g = (value >> 16) & 0xFF;
            b = (value >> 8) & 0xFF;
            a = value & 0xFF;
            break;
        default:
            return std::nullopt;
        }
        return (a << 24) | (r << 16) | (g << 8) | b;
    }

    // ------------------------------------------------------------------ RTF -> text

    std::wstring PlainTextFromRtf(std::string_view rtf)
    {
        struct GroupState
        {
            bool skip = false;
            int unicodeSkip = 1;   // RTF "uc" keyword
        };

        std::wstring out;
        std::string pendingBytes;   // \'hh bytes / 8-bit chars awaiting code page decoding
        std::vector<GroupState> stack;
        GroupState state;
        unsigned int codePage = kCodePageWindows1252;
        int skipCount = 0;          // characters still to skip after a "u" keyword

        auto flush = [&]() {
            if (!pendingBytes.empty())
            {
                out += DecodeCodePage(pendingBytes, codePage);
                pendingBytes.clear();
            }
        };
        auto emit = [&](std::uint32_t codePoint) {
            if (state.skip)
            {
                return;
            }
            flush();
            AppendCodePoint(out, codePoint);
        };

        const std::size_t n = rtf.size();
        std::size_t i = 0;
        while (i < n)
        {
            const char c = rtf[i];
            if (c == '{')
            {
                flush();
                stack.push_back(state);
                skipCount = 0;
                ++i;
                continue;
            }
            if (c == '}')
            {
                flush();
                if (!stack.empty())
                {
                    state = stack.back();
                    stack.pop_back();
                }
                skipCount = 0;
                ++i;
                continue;
            }
            if (c == '\\')
            {
                if (i + 1 >= n)
                {
                    break;
                }
                const char d = rtf[i + 1];
                if (IsAsciiLetter(d))
                {
                    std::size_t j = i + 1;
                    while (j < n && IsAsciiLetter(rtf[j]))
                    {
                        ++j;
                    }
                    const std::string_view word = rtf.substr(i + 1, j - (i + 1));
                    bool hasParam = false;
                    bool negative = false;
                    long long param = 0;
                    if (j < n && rtf[j] == '-' && j + 1 < n && IsAsciiDigit(rtf[j + 1]))
                    {
                        negative = true;
                        ++j;
                    }
                    while (j < n && IsAsciiDigit(rtf[j]))
                    {
                        hasParam = true;
                        if (param < 100000000)
                        {
                            param = param * 10 + (rtf[j] - '0');
                        }
                        ++j;
                    }
                    if (negative)
                    {
                        param = -param;
                    }
                    if (j < n && rtf[j] == ' ')
                    {
                        ++j;   // delimiter
                    }
                    i = j;

                    if (word == "bin")
                    {
                        // Binary data follows; skip it entirely.
                        flush();
                        if (hasParam && param > 0)
                        {
                            i = std::min(n, i + static_cast<std::size_t>(param));
                        }
                        continue;
                    }
                    if (word == "u" && hasParam)
                    {
                        std::int64_t code = param;
                        if (code < 0)
                        {
                            code += 65536;
                        }
                        if (code > 0 && code <= 0xFFFF)
                        {
                            if (!state.skip)
                            {
                                flush();
                                out += static_cast<wchar_t>(code);   // surrogate halves pass through as-is
                            }
                        }
                        skipCount = state.unicodeSkip;
                        continue;
                    }
                    skipCount = 0;
                    if (word == "uc")
                    {
                        state.unicodeSkip = hasParam ? static_cast<int>(std::max<long long>(0, param)) : 1;
                    }
                    else if (word == "ansicpg")
                    {
                        if (hasParam && param > 0 && param < 65536)
                        {
                            flush();
                            codePage = static_cast<unsigned int>(param);
                        }
                    }
                    else if (word == "par" || word == "line" || word == "sect" || word == "page" || word == "row")
                    {
                        emit('\n');
                    }
                    else if (word == "tab" || word == "cell")
                    {
                        emit('\t');
                    }
                    else if (word == "emdash") emit(0x2014);
                    else if (word == "endash") emit(0x2013);
                    else if (word == "emspace") emit(0x2003);
                    else if (word == "enspace") emit(0x2002);
                    else if (word == "qmspace") emit(0x2005);
                    else if (word == "bullet") emit(0x2022);
                    else if (word == "lquote") emit(0x2018);
                    else if (word == "rquote") emit(0x2019);
                    else if (word == "ldblquote") emit(0x201C);
                    else if (word == "rdblquote") emit(0x201D);
                    else if (word == "zwj") emit(0x200D);
                    else if (word == "zwnj") emit(0x200C);
                    else if (IsSkippedRtfDestination(word))
                    {
                        flush();
                        state.skip = true;
                    }
                    continue;
                }

                if (d == '\'')
                {
                    const int hi = (i + 2 < n) ? HexDigit(rtf[i + 2]) : -1;
                    const int lo = (i + 3 < n) ? HexDigit(rtf[i + 3]) : -1;
                    i += (hi >= 0 && lo >= 0) ? 4 : 2;
                    if (hi < 0 || lo < 0)
                    {
                        continue;
                    }
                    if (skipCount > 0)
                    {
                        --skipCount;
                        continue;
                    }
                    if (!state.skip)
                    {
                        pendingBytes.push_back(static_cast<char>((hi << 4) | lo));
                    }
                    continue;
                }

                i += 2;
                switch (d)
                {
                case '*':
                    flush();
                    state.skip = true;   // ignorable destination we don't understand
                    break;
                case '\\':
                case '{':
                case '}':
                    if (skipCount > 0)
                    {
                        --skipCount;
                    }
                    else
                    {
                        emit(static_cast<std::uint32_t>(static_cast<unsigned char>(d)));
                    }
                    break;
                case '~':
                    emit(0xA0);
                    break;
                case '_':
                    emit(0x2011);
                    break;
                case '\n':
                case '\r':
                    emit('\n');   // "\<newline>" is a paragraph break (Cocoa writes these)
                    break;
                default:
                    break;   // \- optional hyphen, \| and other symbols
                }
                continue;
            }

            ++i;
            if (c == '\r' || c == '\n')
            {
                continue;
            }
            if (skipCount > 0)
            {
                --skipCount;
                continue;
            }
            if (state.skip)
            {
                continue;
            }
            if (static_cast<unsigned char>(c) >= 0x80)
            {
                pendingBytes.push_back(c);
            }
            else
            {
                flush();
                out += static_cast<wchar_t>(c);
            }
        }
        flush();
        return out;
    }

    // ------------------------------------------------------------------ CF_HTML -> text

    std::wstring PlainTextFromHtml(std::string_view cfHtml)
    {
        const std::wstring html = DecodeCodePage(CfHtmlFragment(cfHtml), kCodePageUtf8);

        std::wstring out;
        bool pendingSpace = false;
        int preDepth = 0;

        auto flushSpace = [&]() {
            if (pendingSpace && !out.empty() && out.back() != L' ' && out.back() != L'\n' && out.back() != L'\t')
            {
                out += L' ';
            }
            pendingSpace = false;
        };
        // A raw UTF-16 code unit from the document (surrogate halves are kept as they are).
        auto appendUnit = [&](wchar_t unit) {
            flushSpace();
            out += unit;
        };
        // A decoded character reference.
        auto appendCodePoint = [&](std::uint32_t codePoint) {
            flushSpace();
            AppendCodePoint(out, codePoint);
        };
        auto trimTrailingSpaces = [&]() {
            while (!out.empty() && out.back() == L' ')
            {
                out.pop_back();
            }
        };
        auto blockBreak = [&]() {
            pendingSpace = false;
            trimTrailingSpaces();
            if (!out.empty() && out.back() != L'\n')
            {
                out += L'\n';
            }
        };

        const std::size_t n = html.size();
        std::size_t i = 0;
        while (i < n)
        {
            const wchar_t c = html[i];
            if (c == L'<')
            {
                const std::wstring_view rest = std::wstring_view(html).substr(i);
                if (rest.starts_with(L"<!--"))
                {
                    const auto end = html.find(L"-->", i + 4);
                    i = (end == std::wstring::npos) ? n : end + 3;
                    continue;
                }
                if (rest.starts_with(L"<!") || rest.starts_with(L"<?"))
                {
                    const auto end = html.find(L'>', i);
                    i = (end == std::wstring::npos) ? n : end + 1;
                    continue;
                }

                std::size_t j = i + 1;
                const bool closing = j < n && html[j] == L'/';
                if (closing)
                {
                    ++j;
                }
                const std::size_t nameStart = j;
                while (j < n && ((html[j] >= L'a' && html[j] <= L'z') || (html[j] >= L'A' && html[j] <= L'Z') ||
                                 (html[j] >= L'0' && html[j] <= L'9')))
                {
                    ++j;
                }
                if (j == nameStart)
                {
                    // Not a tag (e.g. "a < b"): keep the character.
                    appendUnit(c);
                    ++i;
                    continue;
                }
                const std::wstring name = AsciiLowerW(std::wstring_view(html).substr(nameStart, j - nameStart));

                // Find the end of the tag, honouring quoted attribute values.
                wchar_t quote = 0;
                while (j < n)
                {
                    const wchar_t ch = html[j];
                    if (quote != 0)
                    {
                        if (ch == quote)
                        {
                            quote = 0;
                        }
                    }
                    else if (ch == L'"' || ch == L'\'')
                    {
                        quote = ch;
                    }
                    else if (ch == L'>')
                    {
                        break;
                    }
                    ++j;
                }
                i = (j < n) ? j + 1 : n;

                if (!closing && (name == L"script" || name == L"style" || name == L"head" || name == L"title" ||
                                 name == L"noscript" || name == L"template"))
                {
                    const auto closeTag = FindIgnoreAsciiCase(html, L"</" + name, i);
                    if (closeTag == std::wstring_view::npos)
                    {
                        i = n;
                    }
                    else
                    {
                        const auto end = html.find(L'>', closeTag);
                        i = (end == std::wstring::npos) ? n : end + 1;
                    }
                    continue;
                }

                if (name == L"br")
                {
                    pendingSpace = false;
                    trimTrailingSpaces();
                    out += L'\n';
                }
                else if (IsHtmlBlockTag(name))
                {
                    blockBreak();
                }
                else if ((name == L"td" || name == L"th") && !closing)
                {
                    pendingSpace = false;
                    trimTrailingSpaces();
                    if (!out.empty() && out.back() != L'\n' && out.back() != L'\t')
                    {
                        out += L'\t';
                    }
                }

                if (name == L"pre")
                {
                    preDepth = closing ? std::max(0, preDepth - 1) : preDepth + 1;
                }
                continue;
            }

            if (c == L'&')
            {
                const auto semicolon = html.find(L';', i + 1);
                if (semicolon != std::wstring::npos && semicolon - i <= 12 && semicolon > i + 1)
                {
                    const std::wstring_view entity = std::wstring_view(html).substr(i + 1, semicolon - i - 1);
                    std::optional<std::uint32_t> codePoint;
                    if (entity[0] == L'#')
                    {
                        std::uint32_t value = 0;
                        bool valid = entity.size() > 1;
                        const bool hex = valid && (entity[1] == L'x' || entity[1] == L'X');
                        const std::size_t digitsStart = hex ? 2 : 1;
                        valid = valid && entity.size() > digitsStart;
                        for (std::size_t k = digitsStart; valid && k < entity.size(); ++k)
                        {
                            const auto ch = static_cast<std::uint32_t>(entity[k]);
                            const int digit = ch < 0x80 ? HexDigit(static_cast<int>(ch)) : -1;
                            if (digit < 0 || (!hex && digit > 9))
                            {
                                valid = false;
                                break;
                            }
                            value = value * (hex ? 16u : 10u) + static_cast<std::uint32_t>(digit);
                            if (value > 0x10FFFF)
                            {
                                valid = false;
                            }
                        }
                        if (valid)
                        {
                            codePoint = value;
                        }
                    }
                    else
                    {
                        codePoint = NamedEntity(entity);
                    }
                    if (codePoint)
                    {
                        appendCodePoint(*codePoint);
                        i = semicolon + 1;
                        continue;
                    }
                }
                appendUnit(L'&');
                ++i;
                continue;
            }

            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\f')
            {
                if (preDepth > 0)
                {
                    if (c != L'\r')
                    {
                        pendingSpace = false;
                        out += c;
                    }
                }
                else
                {
                    pendingSpace = true;
                }
                ++i;
                continue;
            }

            appendUnit(c);
            ++i;
        }

        // Trim surrounding whitespace introduced by block boundaries.
        std::size_t start = 0;
        std::size_t end = out.size();
        while (start < end && (out[start] == L' ' || out[start] == L'\n' || out[start] == L'\t'))
        {
            ++start;
        }
        while (end > start && (out[end - 1] == L' ' || out[end - 1] == L'\n' || out[end - 1] == L'\t'))
        {
            --end;
        }
        return out.substr(start, end - start);
    }
}
