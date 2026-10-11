#include "pch.h"
#include "Platform/ClipboardService.h"
#include "Platform/AppInfo.h"
#include "Platform/ImageCodec.h"

#include <cstring>
#include <cwctype>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "shell32.lib")

namespace Kleio
{
    namespace
    {
        using Bytes = std::vector<std::uint8_t>;

        // Maps a content type / format name to a clipboard format id. "CF_*" names map to the
        // predefined formats that are plain HGLOBAL memory; anything else is a registered format.
        UINT FormatId(std::wstring_view name)
        {
            if (name.empty())
            {
                return 0;
            }
            static constexpr std::pair<std::wstring_view, UINT> standardFormats[] = {
                { L"CF_TEXT", CF_TEXT },
                { L"CF_OEMTEXT", CF_OEMTEXT },
                { L"CF_UNICODETEXT", CF_UNICODETEXT },
                { L"CF_HDROP", CF_HDROP },
                { L"CF_DIB", CF_DIB },
                { L"CF_DIBV5", CF_DIBV5 },
                { L"CF_LOCALE", CF_LOCALE },
                { L"CF_RIFF", CF_RIFF },
                { L"CF_WAVE", CF_WAVE },
                { L"CF_SYLK", CF_SYLK },
                { L"CF_DIF", CF_DIF },
            };
            for (auto const& [standardName, id] : standardFormats)
            {
                if (name == standardName)
                {
                    return id;
                }
            }
            if (name.starts_with(L"CF_"))
            {
                return 0; // GDI handle formats (CF_BITMAP, CF_ENHMETAFILE, ...) cannot be stored as bytes
            }
            return RegisterClipboardFormatW(std::wstring{ name }.c_str());
        }

        bool IsAvailable(std::wstring_view name)
        {
            const UINT id = FormatId(name);
            return id != 0 && IsClipboardFormatAvailable(id);
        }

        bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept
        {
            return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
        }

        // OpenClipboard with retries: another process may hold the clipboard for a moment.
        class ClipboardLock
        {
        public:
            explicit ClipboardLock(HWND owner) noexcept
            {
                for (int attempt = 0; attempt < 5; ++attempt)
                {
                    if (OpenClipboard(owner))
                    {
                        m_open = true;
                        return;
                    }
                    Sleep(attempt == 0 ? 5 : 20);
                }
                OutputDebugStringW(L"Kleio: OpenClipboard failed\n");
            }
            ~ClipboardLock()
            {
                if (m_open)
                {
                    CloseClipboard();
                }
            }
            ClipboardLock(ClipboardLock const&) = delete;
            ClipboardLock& operator=(ClipboardLock const&) = delete;

            bool IsOpen() const noexcept { return m_open; }

        private:
            bool m_open = false;
        };

        // Clipboard must be open.
        std::optional<Bytes> ReadFormat(UINT format)
        {
            if (format == 0)
            {
                return std::nullopt;
            }
            const HANDLE handle = GetClipboardData(format);
            if (!handle)
            {
                return std::nullopt;
            }
            const SIZE_T size = GlobalSize(handle);
            void const* data = GlobalLock(handle);
            if (!data)
            {
                return std::nullopt;
            }
            Bytes bytes;
            try
            {
                bytes.assign(static_cast<std::uint8_t const*>(data), static_cast<std::uint8_t const*>(data) + size);
            }
            catch (...)
            {
                GlobalUnlock(handle);
                throw;
            }
            GlobalUnlock(handle);
            return bytes;
        }

        // Text formats are NUL terminated and GlobalSize may include padding.
        Bytes TrimAtNul(Bytes bytes)
        {
            const auto nul = std::find(bytes.begin(), bytes.end(), std::uint8_t{ 0 });
            bytes.erase(nul, bytes.end());
            return bytes;
        }

        std::optional<std::wstring> ReadUnicodeText()
        {
            auto bytes = ReadFormat(CF_UNICODETEXT);
            if (!bytes)
            {
                return std::nullopt;
            }
            const std::size_t maxChars = bytes->size() / sizeof(wchar_t);
            auto const* chars = reinterpret_cast<wchar_t const*>(bytes->data());
            return std::wstring(chars, wcsnlen(chars, maxChars));
        }

        std::vector<std::wstring> ReadFiles()
        {
            std::vector<std::wstring> paths;
            const auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
            if (!drop)
            {
                return paths;
            }
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i)
            {
                const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                if (length == 0)
                {
                    continue;
                }
                std::wstring path(length + 1, L'\0');
                const UINT copied = DragQueryFileW(drop, i, path.data(), length + 1);
                path.resize(copied);
                if (!path.empty())
                {
                    paths.push_back(std::move(path));
                }
            }
            return paths;
        }

        std::string_view AsChars(Bytes const& bytes) noexcept
        {
            return { reinterpret_cast<char const*>(bytes.data()), bytes.size() };
        }

        bool IsBlank(std::wstring const& text) noexcept
        {
            return std::all_of(text.begin(), text.end(), [](wchar_t c) { return std::iswspace(c) != 0; });
        }

        // Clipboard must be open (and owned after EmptyClipboard).
        bool SetData(UINT format, void const* data, std::size_t size, std::size_t zeroPadding = 0)
        {
            if (format == 0)
            {
                return false;
            }
            const std::size_t total = (std::max)(size + zeroPadding, std::size_t{ 1 });
            wil::unique_hglobal memory{ GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, total) };
            if (!memory)
            {
                return false;
            }
            void* target = GlobalLock(memory.get());
            if (!target)
            {
                return false;
            }
            if (size > 0)
            {
                std::memcpy(target, data, size);
            }
            GlobalUnlock(memory.get());
            if (!SetClipboardData(format, memory.get()))
            {
                OutputDebugStringW(L"Kleio: SetClipboardData failed\n");
                return false;
            }
            memory.release(); // owned by the system now
            return true;
        }

        bool SetBytes(UINT format, Bytes const& bytes, std::size_t zeroPadding = 0)
        {
            return SetData(format, bytes.data(), bytes.size(), zeroPadding);
        }

        void SetFiles(std::vector<std::wstring> const& paths)
        {
            if (paths.empty())
            {
                return;
            }
            // DROPFILES followed by NUL-separated wide paths and a final extra NUL.
            std::wstring list;
            for (auto const& path : paths)
            {
                if (!path.empty())
                {
                    list.append(path);
                    list.push_back(L'\0');
                }
            }
            if (list.empty())
            {
                return;
            }
            list.push_back(L'\0');

            Bytes buffer(sizeof(DROPFILES) + list.size() * sizeof(wchar_t));
            DROPFILES header{};
            header.pFiles = sizeof(DROPFILES);
            header.fWide = TRUE;
            std::memcpy(buffer.data(), &header, sizeof(header));
            std::memcpy(buffer.data() + sizeof(header), list.data(), list.size() * sizeof(wchar_t));
            if (SetBytes(CF_HDROP, buffer))
            {
                const DWORD effect = DROPEFFECT_COPY;
                SetData(FormatId(L"Preferred DropEffect"), &effect, sizeof(effect));
            }
        }

        std::vector<std::wstring> SplitLines(std::wstring const& text)
        {
            std::vector<std::wstring> lines;
            std::size_t start = 0;
            while (start <= text.size())
            {
                const auto end = text.find(L'\n', start);
                std::wstring line = text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
                if (!line.empty() && line.back() == L'\r')
                {
                    line.pop_back();
                }
                if (!line.empty())
                {
                    lines.push_back(std::move(line));
                }
                if (end == std::wstring::npos)
                {
                    break;
                }
                start = end + 1;
            }
            return lines;
        }

        void WriteContent(HistoryItemContent const& content)
        {
            const std::wstring_view type = content.type;
            if (type == ContentType::FromKleio || type == ContentType::CanIncludeInClipboardHistory)
            {
                return; // markers: FromKleio is written separately; CanIncludeInClipboardHistory never
            }
            if (type == ContentType::Text)
            {
                SetBytes(CF_UNICODETEXT, content.value, sizeof(wchar_t));
            }
            else if (type == ContentType::Html || type == ContentType::Rtf)
            {
                SetBytes(FormatId(type), TrimAtNul(content.value), 1);
            }
            else if (type == ContentType::Files)
            {
                SetFiles(SplitLines(DecodeText(content.value)));
            }
            else if (type == ContentType::Png)
            {
                if (content.value.empty())
                {
                    return;
                }
                SetBytes(FormatId(ContentType::Png), content.value);
                auto dib = ImageCodec::PngToDibV5(content.value);
                if (!dib.empty())
                {
                    SetBytes(CF_DIBV5, dib);
                }
            }
            else
            {
                const UINT id = FormatId(type);
                if (id != 0 && id != CF_HDROP && id != CF_UNICODETEXT)
                {
                    SetBytes(id, content.value);
                }
            }
        }

        void WriteFromKleioMarker()
        {
            const DWORD marker = 0;
            SetData(FormatId(ContentType::FromKleio), &marker, sizeof(marker));
        }
    }

    ClipboardService::ClipboardService(MessageWindow& window, Settings& settings)
        : m_window(window), m_settings(settings)
    {
        m_lastSequence = GetClipboardSequenceNumber();
        m_handlerToken = m_window.AddHandler([this](UINT message, WPARAM, LPARAM) -> std::optional<LRESULT> {
            if (message != WM_CLIPBOARDUPDATE)
            {
                return std::nullopt;
            }
            try
            {
                OnClipboardUpdate();
            }
            catch (...)
            {
                OutputDebugStringW(L"Kleio: exception while processing a clipboard update\n");
            }
            return LRESULT{ 0 };
        });
        if (!AddClipboardFormatListener(m_window.Handle()))
        {
            OutputDebugStringW(L"Kleio: AddClipboardFormatListener failed\n");
        }
    }

    ClipboardService::~ClipboardService()
    {
        if (m_window.Handle())
        {
            RemoveClipboardFormatListener(m_window.Handle());
        }
        m_window.RemoveHandler(m_handlerToken);
    }

    std::uint32_t ClipboardService::ChangeCount() const noexcept
    {
        return GetClipboardSequenceNumber();
    }

    void ClipboardService::OnClipboardUpdate()
    {
        const std::uint32_t sequence = GetClipboardSequenceNumber();
        if (sequence == m_lastSequence)
        {
            return;
        }
        m_lastSequence = sequence;

        const bool fromKleio = IsAvailable(ContentType::FromKleio);
        if (!fromKleio)
        {
            // External copy occurred: stop the current paste stack.
            ExternalChange();
        }

        // 1. Paused / ignore next copy.
        if (m_settings.IgnoreEvents())
        {
            if (m_settings.IgnoreOnlyNextEvent())
            {
                m_settings.IgnoreEvents(false);
                m_settings.IgnoreOnlyNextEvent(false);
            }
            return;
        }

        // 2. Ignored formats.
        for (auto const& format : AlwaysIgnoredFormats)
        {
            if (IsAvailable(format))
            {
                return;
            }
        }
        for (auto const& format : m_settings.IgnoredFormats())
        {
            if (IsAvailable(format))
            {
                return;
            }
        }

        // 3. Ignored applications. Kleio's own copies are owned by Kleio itself (Maccy sees the
        //    frontmost app there), so they are not subject to the app filter.
        std::wstring application = AppInfo::ProcessPathForWindow(GetClipboardOwner());
        if (application.empty())
        {
            application = AppInfo::ForegroundProcessPath();
        }
        if (!fromKleio && !application.empty())
        {
            const std::wstring fileName = AppInfo::FileName(application);
            bool listed = false;
            for (auto const& entry : m_settings.IgnoredApps())
            {
                if (!entry.empty() && (EqualsIgnoreCase(entry, application) || EqualsIgnoreCase(entry, fileName)))
                {
                    listed = true;
                    break;
                }
            }
            if (m_settings.IgnoreAllAppsExceptListed() ? !listed : listed)
            {
                return;
            }
        }

        ClipboardLock lock{ m_window.Handle() };
        if (!lock.IsOpen())
        {
            return;
        }

        // "CanIncludeInClipboardHistory" = 0 is the Windows convention for "don't record this".
        if (!fromKleio)
        {
            if (auto value = ReadFormat(FormatId(ContentType::CanIncludeInClipboardHistory)); value && value->size() >= sizeof(DWORD))
            {
                DWORD allowed = 1;
                std::memcpy(&allowed, value->data(), sizeof(allowed));
                if (allowed == 0)
                {
                    return;
                }
            }
        }

        // 4. Read the supported types.
        std::optional<std::wstring> text;
        if (IsClipboardFormatAvailable(CF_UNICODETEXT)) // also synthesized from CF_TEXT / CF_OEMTEXT
        {
            text = ReadUnicodeText();
        }
        std::optional<Bytes> html;
        std::optional<Bytes> rtf;
        if (IsAvailable(ContentType::Html))
        {
            if (auto bytes = ReadFormat(FormatId(ContentType::Html)))
            {
                html = TrimAtNul(std::move(*bytes));
            }
        }
        if (IsAvailable(ContentType::Rtf))
        {
            if (auto bytes = ReadFormat(FormatId(ContentType::Rtf)))
            {
                rtf = TrimAtNul(std::move(*bytes));
            }
        }

        if (text)
        {
            // Whitespace-only text without rich text is not worth keeping.
            if (IsBlank(*text))
            {
                const bool richText = (rtf && !rtf->empty() && !PlainTextFromRtf(AsChars(*rtf)).empty()) ||
                    (html && !html->empty() && !PlainTextFromHtml(AsChars(*html)).empty());
                if (!richText)
                {
                    return;
                }
            }

            // 5. Ignore regexps (an invalid pattern stops the check, like Maccy).
            for (auto const& pattern : m_settings.IgnoreRegexps())
            {
                if (pattern.empty())
                {
                    continue;
                }
                try
                {
                    const std::wregex regex{ pattern };
                    if (std::regex_search(*text, regex))
                    {
                        return;
                    }
                }
                catch (...)
                {
                    break;
                }
            }
        }

        std::vector<HistoryItemContent> contents;
        const bool saveText = m_settings.SaveText();
        if (saveText)
        {
            if (text && !text->empty())
            {
                contents.push_back({ std::wstring{ ContentType::Text }, EncodeText(*text) });
            }
            if (html && !html->empty())
            {
                contents.push_back({ std::wstring{ ContentType::Html }, std::move(*html) });
            }
            if (rtf && !rtf->empty())
            {
                contents.push_back({ std::wstring{ ContentType::Rtf }, std::move(*rtf) });
            }
        }

        bool hasFiles = false;
        if (m_settings.SaveFiles() && IsClipboardFormatAvailable(CF_HDROP))
        {
            auto files = ReadFiles();
            if (!files.empty())
            {
                hasFiles = true;
                contents.push_back({ std::wstring{ ContentType::Files }, EncodeFiles(files) });
            }
        }

        // Images. Windows apps (Office in particular) attach rendered pictures to plain text copies,
        // so images are only recorded when the copy carries no meaningful text or files.
        const bool hasMeaningfulText = saveText && text && !IsBlank(*text);
        if (m_settings.SaveImages() && !hasMeaningfulText && !hasFiles)
        {
            Bytes png;
            if (IsAvailable(ContentType::Png))
            {
                if (auto bytes = ReadFormat(FormatId(ContentType::Png)))
                {
                    png = std::move(*bytes);
                }
            }
            if (png.empty() && IsClipboardFormatAvailable(CF_DIBV5))
            {
                if (auto dib = ReadFormat(CF_DIBV5))
                {
                    png = ImageCodec::DibToPng(*dib);
                }
            }
            if (png.empty() && IsClipboardFormatAvailable(CF_DIB))
            {
                if (auto dib = ReadFormat(CF_DIB))
                {
                    png = ImageCodec::DibToPng(*dib);
                }
            }
            if (!png.empty())
            {
                contents.push_back({ std::wstring{ ContentType::Png }, std::move(png) });
            }
        }

        if (contents.empty())
        {
            return;
        }
        if (fromKleio)
        {
            contents.push_back({ std::wstring{ ContentType::FromKleio }, {} });
        }

        auto item = std::make_shared<HistoryItem>();
        item->application = std::move(application);
        item->firstCopiedAt = Now();
        item->lastCopiedAt = item->firstCopiedAt;
        item->numberOfCopies = 1;
        item->contents = std::move(contents);
        item->title = item->GenerateTitle(m_settings.ShowSpecialSymbols());

        NewCopy(item);
    }

    void ClipboardService::Copy(HistoryItem const& item, bool removeFormatting)
    {
        try
        {
            std::vector<HistoryItemContent const*> contents;
            for (auto const& content : item.contents)
            {
                contents.push_back(&content);
            }
            // Like Maccy's clearFormatting: keep plain text (and files); without text, keep everything.
            if (removeFormatting && item.Has(ContentType::Text))
            {
                std::erase_if(contents, [](HistoryItemContent const* content) {
                    return content->type != ContentType::Text && content->type != ContentType::Files;
                });
            }

            ClipboardLock lock{ m_window.Handle() };
            if (!lock.IsOpen())
            {
                return;
            }
            if (!EmptyClipboard())
            {
                OutputDebugStringW(L"Kleio: EmptyClipboard failed\n");
                return;
            }
            for (auto const* content : contents)
            {
                WriteContent(*content);
            }
            WriteFromKleioMarker();
        }
        catch (...)
        {
            OutputDebugStringW(L"Kleio: exception while copying an item\n");
        }
        // WM_CLIPBOARDUPDATE follows and records the copy (marked FromKleio), like Maccy's
        // checkForChangesInPasteboard after copy().
    }

    void ClipboardService::CopyText(std::wstring const& text)
    {
        try
        {
            ClipboardLock lock{ m_window.Handle() };
            if (!lock.IsOpen() || !EmptyClipboard())
            {
                return;
            }
            // Like Maccy's copyInMaccy: no FromKleio marker, so the new text is recorded as a regular copy.
            SetBytes(CF_UNICODETEXT, EncodeText(text), sizeof(wchar_t));
        }
        catch (...)
        {
            OutputDebugStringW(L"Kleio: exception while copying text\n");
        }
    }

    void ClipboardService::ClearIfEnabled()
    {
        try
        {
            if (!m_settings.ClearSystemClipboard())
            {
                return;
            }
            ClipboardLock lock{ m_window.Handle() };
            if (lock.IsOpen())
            {
                EmptyClipboard();
            }
        }
        catch (...)
        {
        }
    }
}
