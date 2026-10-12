#include "pch.h"
#include "Platform/ClipboardService.h"
#include "Core/ClipboardFormats.h"
#include "Platform/AppInfo.h"
#include "Platform/ImageCodec.h"

#include <cstring>
#include <cwctype>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")

namespace Kleio
{
    namespace
    {
        using Bytes = std::vector<std::uint8_t>;
        using ClipboardFormats::Kind;

        // WM_TIMER id on the shared message window (no other service uses timers on it).
        constexpr UINT_PTR kCheckTimerId = 0x4B6C;
        // Maccy polls the pasteboard every clipboardCheckInterval (0.5 s), so a burst of changes
        // within one interval is seen as one copy. Kleio waits until the clipboard has been quiet
        // for kSettleDelay (bounded by kMaxDelay after the first notification) and then checks once.
        constexpr ULONGLONG kSettleDelayMs = 250;
        constexpr ULONGLONG kMaxDelayMs = 1000;
        constexpr ULONGLONG kBusyRetryMs = 50;
        constexpr int kMaxOpenRetries = 5;

        // Maps a content type / format name to a clipboard format id. "CF_*" names map to the
        // predefined formats; anything else is a registered format.
        UINT FormatId(std::wstring_view name)
        {
            if (name.empty())
            {
                return 0;
            }
            if (const UINT id = ClipboardFormats::StandardFormatId(name); id != 0)
            {
                return id;
            }
            if (name.starts_with(L"CF_"))
            {
                return 0;
            }
            return RegisterClipboardFormatW(std::wstring{ name }.c_str());
        }

        // Stored name of a format present on the clipboard ("" when it has none).
        std::wstring FormatName(UINT id)
        {
            if (id >= 0xC000 && id <= 0xFFFF)
            {
                wchar_t buffer[256] = {};
                const int length = GetClipboardFormatNameW(id, buffer, static_cast<int>(std::size(buffer)));
                return length > 0 ? std::wstring(buffer, static_cast<std::size_t>(length)) : std::wstring{};
            }
            return ClipboardFormats::StandardFormatName(id);
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
            explicit ClipboardLock(HWND owner, int attempts = 5) noexcept
            {
                for (int attempt = 0; attempt < attempts; ++attempt)
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

        // Clipboard must be open. Reads an HGLOBAL format (rendering it if it is delay-rendered).
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
            if (size > ClipboardFormats::MaxFormatSize)
            {
                OutputDebugStringW(L"Kleio: clipboard format too large to store, skipped\n");
                return std::nullopt;
            }
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

        // Clipboard must be open. CF_ENHMETAFILE is a GDI handle; its bits are what we store.
        std::optional<Bytes> ReadEnhMetafile()
        {
            const auto metafile = static_cast<HENHMETAFILE>(GetClipboardData(CF_ENHMETAFILE));
            if (!metafile)
            {
                return std::nullopt;
            }
            const UINT size = GetEnhMetaFileBits(metafile, 0, nullptr);
            if (size == 0 || size > ClipboardFormats::MaxFormatSize)
            {
                return std::nullopt;
            }
            Bytes bytes(size);
            if (GetEnhMetaFileBits(metafile, size, bytes.data()) != size)
            {
                return std::nullopt;
            }
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

        // Clipboard must be open. OLE puts the HWND of its clipboard window under "DataObject"
        // while the source app's IDataObject is live on the clipboard (OleSetClipboard) and clears
        // it when OleFlushClipboard renders everything. nullopt when the format is absent.
        std::optional<HWND> ReadOleDataObjectWindow()
        {
            const UINT format = FormatId(L"DataObject");
            if (format == 0 || !IsClipboardFormatAvailable(format))
            {
                return std::nullopt;
            }
            const auto bytes = ReadFormat(format);
            if (!bytes)
            {
                return HWND{};
            }
            std::uintptr_t value = 0;
            std::memcpy(&value, bytes->data(), (std::min)(bytes->size(), sizeof(value)));
            return reinterpret_cast<HWND>(value);
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

        void SetEnhMetafile(Bytes const& bytes)
        {
            if (bytes.empty())
            {
                return;
            }
            const HENHMETAFILE metafile = SetEnhMetaFileBits(static_cast<UINT>(bytes.size()), bytes.data());
            if (!metafile)
            {
                return;
            }
            if (!SetClipboardData(CF_ENHMETAFILE, metafile))
            {
                DeleteEnhMetaFile(metafile);
            }
            // On success the system owns the metafile (CF_METAFILEPICT is synthesized from it).
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

        // Writes one stored content back under its format (Maccy's pasteboard.setData per type).
        void WriteContent(HistoryItemContent const& content)
        {
            switch (ClipboardFormats::ClassifyStored(content.type))
            {
            case Kind::Text:
                SetBytes(CF_UNICODETEXT, content.value, sizeof(wchar_t));
                break;
            case Kind::Html:
            case Kind::Rtf:
                SetBytes(FormatId(content.type), TrimAtNul(content.value), 1);
                break;
            case Kind::Files:
                SetFiles(SplitLines(DecodeText(content.value)));
                break;
            case Kind::Png:
                if (!content.value.empty())
                {
                    SetBytes(FormatId(ContentType::Png), content.value);
                    // CF_DIB and CF_BITMAP are synthesized by Windows from CF_DIBV5.
                    auto dib = ImageCodec::PngToDibV5(content.value);
                    if (!dib.empty())
                    {
                        SetBytes(CF_DIBV5, dib);
                    }
                }
                break;
            case Kind::Metafile:
                SetEnhMetafile(content.value);
                break;
            case Kind::Image:
            case Kind::Raw:
                SetBytes(FormatId(content.type), content.value);
                break;
            case Kind::Bitmap:
            case Kind::Skip:
                // Never stored; markers (FromKleio is written separately, CanIncludeInClipboardHistory never).
                break;
            }
        }

        void WriteFromKleioMarker()
        {
            const DWORD marker = 0;
            SetData(FormatId(ContentType::FromKleio), &marker, sizeof(marker));
        }

        struct ClipboardFormat
        {
            UINT id = 0;
            std::wstring name;
            Kind kind = Kind::Skip;
        };

        // Clipboard must be open. In the order the formats were put on the clipboard (which is
        // the order they are restored in), including formats synthesized by Windows.
        std::vector<ClipboardFormat> EnumerateFormats()
        {
            std::vector<ClipboardFormat> formats;
            UINT id = 0;
            while ((id = EnumClipboardFormats(id)) != 0)
            {
                ClipboardFormat format;
                format.id = id;
                format.name = FormatName(id);
                format.kind = ClipboardFormats::Classify(id, format.name);
                formats.push_back(std::move(format));
            }
            return formats;
        }

        bool IsEnabled(ClipboardFormats::SaveSetting setting, Settings const& settings)
        {
            switch (setting)
            {
            case ClipboardFormats::SaveSetting::Text:
                return settings.SaveText();
            case ClipboardFormats::SaveSetting::Files:
                return settings.SaveFiles();
            case ClipboardFormats::SaveSetting::Images:
                return settings.SaveImages();
            default:
                return true;
            }
        }
    }

    ClipboardService::ClipboardService(MessageWindow& window, Settings& settings)
        : m_window(window), m_settings(settings)
    {
        m_lastSequence = GetClipboardSequenceNumber();
        m_handlerToken = m_window.AddHandler([this](UINT message, WPARAM wParam, LPARAM) -> std::optional<LRESULT> {
            if (message == WM_CLIPBOARDUPDATE)
            {
                try
                {
                    OnClipboardUpdate();
                }
                catch (...)
                {
                    OutputDebugStringW(L"Kleio: exception while scheduling a clipboard check\n");
                }
                return LRESULT{ 0 };
            }
            if (message == WM_TIMER && wParam == kCheckTimerId)
            {
                try
                {
                    OnCheckTimer();
                }
                catch (...)
                {
                    OutputDebugStringW(L"Kleio: exception while processing a clipboard update\n");
                }
                return LRESULT{ 0 };
            }
            return std::nullopt;
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
            KillTimer(m_window.Handle(), kCheckTimerId);
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
        ++m_notifications;
        const ULONGLONG now = GetTickCount64();
        if (m_pendingSince == 0)
        {
            m_pendingSince = now;
        }
        // Restart the settle delay, without postponing the check beyond kMaxDelayMs.
        const ULONGLONG elapsed = now - m_pendingSince;
        const ULONGLONG remaining = elapsed >= kMaxDelayMs ? 0 : kMaxDelayMs - elapsed;
        ScheduleCheck((std::min)(kSettleDelayMs, remaining));
    }

    void ClipboardService::ScheduleCheck(ULONGLONG delayMs)
    {
        if (m_pendingSince == 0)
        {
            m_pendingSince = GetTickCount64();
        }
        const UINT delay = static_cast<UINT>((std::max<ULONGLONG>)(delayMs, USER_TIMER_MINIMUM));
        if (!SetTimer(m_window.Handle(), kCheckTimerId, delay, nullptr))
        {
            // No timer: check right away rather than miss the copy.
            OutputDebugStringW(L"Kleio: SetTimer failed\n");
            m_pendingSince = 0;
            CheckForChanges();
        }
    }

    void ClipboardService::OnCheckTimer()
    {
        // Someone is still writing (between OpenClipboard and CloseClipboard): wait a bit more.
        if (GetOpenClipboardWindow() != nullptr && GetTickCount64() - m_pendingSince < kMaxDelayMs)
        {
            ScheduleCheck(kBusyRetryMs);
            return;
        }
        KillTimer(m_window.Handle(), kCheckTimerId);
        m_pendingSince = 0;
        CheckForChanges();
    }

    void ClipboardService::CheckForChanges()
    {
        const std::uint32_t sequence = GetClipboardSequenceNumber();
        if (sequence == m_lastSequence)
        {
            m_notifications = 0;
            return;
        }

        bool externalChange = false;
        HistoryItemPtr item;
        {
            ClipboardLock lock{ m_window.Handle(), 2 };
            if (!lock.IsOpen())
            {
                // Held by another app: look again shortly instead of losing the copy.
                if (++m_openRetries <= kMaxOpenRetries)
                {
                    ScheduleCheck(kSettleDelayMs);
                }
                else
                {
                    m_openRetries = 0;
                    m_lastSequence = sequence;
                }
                return;
            }
            m_openRetries = 0;
            m_lastSequence = sequence;
            const int notifications = std::exchange(m_notifications, 0);
            item = ReadCopy(sequence, notifications, externalChange);
        }

        if (externalChange)
        {
            // External copy occurred: stop the current paste stack.
            ExternalChange();
        }
        if (item)
        {
            NewCopy(item);
        }
    }

    // Clipboard is open. Port of the body of Maccy's checkForChangesInPasteboard.
    HistoryItemPtr ClipboardService::ReadCopy(std::uint32_t sequence, int notifications, bool& externalChange)
    {
        // OleFlushClipboard after OleSetClipboard (WinForms, Office, Explorer) changes the clipboard
        // a second time without a new copy. When it comes too late to be coalesced, recognise it:
        // a lone notification, the same OLE clipboard window as owner, and the live data object is
        // gone. (A new OLE copy always puts a live data object on the clipboard.)
        const HWND owner = GetClipboardOwner();
        const auto oleDataObject = ReadOleDataObjectWindow();
        const bool liveOle = oleDataObject && *oleDataObject != nullptr;
        if (!liveOle && m_liveOleOwner != nullptr && owner == m_liveOleOwner && notifications <= 1)
        {
            m_liveOleOwner = nullptr;
            return nullptr;
        }
        m_liveOleOwner = liveOle && owner ? owner : nullptr;

        const bool fromKleio = IsAvailable(ContentType::FromKleio);
        externalChange = !fromKleio;

        // 1. Paused / ignore next copy.
        if (m_settings.IgnoreEvents())
        {
            if (m_settings.IgnoreOnlyNextEvent())
            {
                m_settings.IgnoreEvents(false);
                m_settings.IgnoreOnlyNextEvent(false);
            }
            return nullptr;
        }

        // 2. Ignored formats, and copies without any enabled type (Maccy's shouldIgnore(types)).
        for (auto const& format : AlwaysIgnoredFormats)
        {
            if (IsAvailable(format))
            {
                return nullptr;
            }
        }
        for (auto const& format : m_settings.IgnoredFormats())
        {
            if (IsAvailable(format))
            {
                return nullptr;
            }
        }
        const auto formats = EnumerateFormats();
        const bool hasEnabledType = std::any_of(formats.begin(), formats.end(), [this](ClipboardFormat const& format) {
            const auto setting = ClipboardFormats::SaveSettingOf(format.kind);
            return setting != ClipboardFormats::SaveSetting::None && IsEnabled(setting, m_settings);
        }) || (m_settings.SaveText() && IsClipboardFormatAvailable(CF_UNICODETEXT)) ||
            (m_settings.SaveImages() && IsClipboardFormatAvailable(CF_DIB));
        if (!hasEnabledType)
        {
            return nullptr;
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
                    return nullptr;
                }
            }
        }

        // 3. Ignored applications. Kleio's own copies are owned by Kleio itself (Maccy sees the
        //    frontmost app there), so they are not subject to the app filter.
        std::wstring application = AppInfo::ProcessPathForWindow(owner);
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
                return nullptr;
            }
        }

        // 4. Whitespace-only text without rich text is not worth keeping; 5. ignore regexps.
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
            if (IsBlank(*text))
            {
                const bool richText = (rtf && !rtf->empty() && !PlainTextFromRtf(AsChars(*rtf)).empty()) ||
                    (html && !html->empty() && !PlainTextFromHtml(AsChars(*html)).empty());
                if (!richText)
                {
                    return nullptr;
                }
            }

            // An invalid pattern stops the check, like Maccy.
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
                        return nullptr;
                    }
                }
                catch (...)
                {
                    break;
                }
            }
        }

        // 6. Every persistable format, minus disabled types, in clipboard order (Maccy:
        //    types.subtracting(disabledTypes).filter(isPersistable)).
        std::vector<HistoryItemContent> contents;
        auto has = [&contents](std::wstring_view type) {
            return std::any_of(contents.begin(), contents.end(), [type](HistoryItemContent const& c) { return c.type == type; });
        };
        const bool hasPngFormat = std::any_of(formats.begin(), formats.end(), [](ClipboardFormat const& format) {
            return format.kind == Kind::Png;
        });
        for (auto const& format : formats)
        {
            Kind kind = format.kind;
            if (format.id == CF_TEXT || format.id == CF_OEMTEXT)
            {
                kind = Kind::Text;     // the text is stored once, as CF_UNICODETEXT
            }
            else if (format.id == CF_BITMAP)
            {
                kind = Kind::Bitmap;   // the image is stored once, as PNG
            }
            if (kind == Kind::Skip || !IsEnabled(ClipboardFormats::SaveSettingOf(kind), m_settings))
            {
                continue;
            }
            switch (kind)
            {
            case Kind::Text:
                if (text && !text->empty() && !has(ContentType::Text))
                {
                    contents.push_back({ std::wstring{ ContentType::Text }, EncodeText(*text) });
                }
                break;
            case Kind::Html:
                if (html && !html->empty() && !has(ContentType::Html))
                {
                    contents.push_back({ std::wstring{ ContentType::Html }, std::move(*html) });
                }
                break;
            case Kind::Rtf:
                if (rtf && !rtf->empty() && !has(ContentType::Rtf))
                {
                    contents.push_back({ std::wstring{ ContentType::Rtf }, std::move(*rtf) });
                }
                break;
            case Kind::Files:
                if (!has(ContentType::Files))
                {
                    if (auto files = ReadFiles(); !files.empty())
                    {
                        contents.push_back({ std::wstring{ ContentType::Files }, EncodeFiles(files) });
                    }
                }
                break;
            case Kind::Png:
                if (!has(ContentType::Png))
                {
                    if (auto bytes = ReadFormat(format.id); bytes && !bytes->empty())
                    {
                        contents.push_back({ std::wstring{ ContentType::Png }, std::move(*bytes) });
                    }
                }
                break;
            case Kind::Bitmap:
                // Kleio's image representation is PNG; CF_DIB / CF_DIBV5 / CF_BITMAP are
                // synthesized again from the CF_DIBV5 written on restore.
                if (!hasPngFormat && !has(ContentType::Png))
                {
                    Bytes png;
                    for (const UINT dibFormat : { UINT{ CF_DIBV5 }, UINT{ CF_DIB } })
                    {
                        if (png.empty() && IsClipboardFormatAvailable(dibFormat))
                        {
                            if (auto dib = ReadFormat(dibFormat))
                            {
                                png = ImageCodec::DibToPng(*dib);
                            }
                        }
                    }
                    if (!png.empty())
                    {
                        contents.push_back({ std::wstring{ ContentType::Png }, std::move(png) });
                    }
                }
                break;
            case Kind::Metafile:
                if (!has(ClipboardFormats::EnhMetafile))
                {
                    if (auto bytes = ReadEnhMetafile())
                    {
                        contents.push_back({ std::wstring{ ClipboardFormats::EnhMetafile }, std::move(*bytes) });
                    }
                }
                break;
            case Kind::Image:
            case Kind::Raw:
                if (!format.name.empty() && !has(format.name))
                {
                    // Formats without data (delay rendering that fails, non-memory handles) are skipped.
                    if (auto bytes = ReadFormat(format.id))
                    {
                        contents.push_back({ format.name, std::move(*bytes) });
                    }
                }
                break;
            case Kind::Skip:
                break;
            }
        }
        // CF_UNICODETEXT that was not enumerated (should not happen) is still kept.
        if (m_settings.SaveText() && text && !text->empty() && !has(ContentType::Text))
        {
            contents.insert(contents.begin(), HistoryItemContent{ std::wstring{ ContentType::Text }, EncodeText(*text) });
        }

        if (contents.empty())
        {
            return nullptr;
        }
        if (fromKleio)
        {
            contents.push_back({ std::wstring{ ContentType::FromKleio }, {} });
        }

        auto copy = std::make_shared<HistoryItem>();
        copy->application = std::move(application);
        copy->firstCopiedAt = Now();
        copy->lastCopiedAt = copy->firstCopiedAt;
        copy->numberOfCopies = 1;
        copy->contents = std::move(contents);
        copy->changeCount = sequence;
        copy->title = copy->GenerateTitle(m_settings.ShowSpecialSymbols());
        return copy;
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

            {
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
                // Every stored format, in the order it was copied (Maccy's setData per content).
                for (auto const* content : contents)
                {
                    WriteContent(*content);
                }
                WriteFromKleioMarker();
            }
            // Record the copy (marked FromKleio) right away, like Maccy's checkForChangesInPasteboard
            // after copy(), instead of waiting for the settle delay.
            ScheduleCheck(0);
        }
        catch (...)
        {
            OutputDebugStringW(L"Kleio: exception while copying an item\n");
        }
    }

    void ClipboardService::CopyText(std::wstring const& text)
    {
        try
        {
            {
                ClipboardLock lock{ m_window.Handle() };
                if (!lock.IsOpen() || !EmptyClipboard())
                {
                    return;
                }
                // Like Maccy's copyInMaccy: no FromKleio marker, so the new text is recorded as a regular copy.
                SetBytes(CF_UNICODETEXT, EncodeText(text), sizeof(wchar_t));
            }
            ScheduleCheck(0);
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
