#include "pch.h"
#include "Core/Settings.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <system_error>

// NOTE: no WinRT in this file. The LocalSettings backend lives in Core/AppData.cpp so that the
// unit tests can link Settings with an in-memory store.

namespace Kleio
{
    namespace
    {
        // ---- scalar (de)serialization ----

        std::wstring ToWide(std::string_view ascii)
        {
            return std::wstring(ascii.begin(), ascii.end());
        }

        // Converts an ASCII-only wide string to narrow; returns false for anything else.
        bool ToAscii(std::wstring_view text, std::string& out)
        {
            out.clear();
            out.reserve(text.size());
            for (wchar_t c : text)
            {
                if (c == 0 || static_cast<std::uint32_t>(c) > 0x7F)
                {
                    return false;
                }
                out.push_back(static_cast<char>(c));
            }
            return true;
        }

        std::wstring_view TrimAscii(std::wstring_view text) noexcept
        {
            while (!text.empty() && (text.front() == L' ' || text.front() == L'\t'))
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && (text.back() == L' ' || text.back() == L'\t'))
            {
                text.remove_suffix(1);
            }
            return text;
        }

        std::wstring FormatBool(bool value)
        {
            return value ? L"1" : L"0";
        }

        std::optional<bool> ParseBool(std::wstring_view text)
        {
            text = TrimAscii(text);
            if (text == L"1" || text == L"true" || text == L"True")
            {
                return true;
            }
            if (text == L"0" || text == L"false" || text == L"False")
            {
                return false;
            }
            return std::nullopt;
        }

        std::wstring FormatInt(std::int32_t value)
        {
            return std::to_wstring(value);
        }

        std::optional<std::int32_t> ParseInt(std::wstring_view text)
        {
            std::string narrow;
            if (!ToAscii(TrimAscii(text), narrow) || narrow.empty())
            {
                return std::nullopt;
            }
            std::int64_t value = 0;
            const char* first = narrow.data();
            const char* last = narrow.data() + narrow.size();
            if (*first == '+')
            {
                ++first;
            }
            auto [ptr, ec] = std::from_chars(first, last, value);
            if (ec != std::errc{} || ptr != last)
            {
                return std::nullopt;
            }
            value = std::clamp<std::int64_t>(value, (std::numeric_limits<std::int32_t>::min)(), (std::numeric_limits<std::int32_t>::max)());
            return static_cast<std::int32_t>(value);
        }

        std::wstring FormatDouble(double value)
        {
            // Shortest round-trip representation, locale independent.
            char buffer[64] = {};
            auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer) - 1, value);
            if (ec != std::errc{})
            {
                return L"0";
            }
            return ToWide(std::string_view(buffer, static_cast<std::size_t>(ptr - buffer)));
        }

        std::optional<double> ParseDouble(std::wstring_view text)
        {
            std::string narrow;
            if (!ToAscii(TrimAscii(text), narrow) || narrow.empty())
            {
                return std::nullopt;
            }
            const char* first = narrow.data();
            const char* last = narrow.data() + narrow.size();
            if (*first == '+')
            {
                ++first;
            }
            double value = 0;
            auto [ptr, ec] = std::from_chars(first, last, value);
            if (ec != std::errc{} || ptr != last || !std::isfinite(value))
            {
                return std::nullopt;
            }
            return value;
        }

        // ---- tiny JSON string-array codec ----

        std::wstring EncodeJsonStringArray(std::vector<std::wstring> const& values)
        {
            static constexpr wchar_t kHex[] = L"0123456789abcdef";
            std::wstring json = L"[";
            bool first = true;
            for (auto const& value : values)
            {
                if (!first)
                {
                    json += L',';
                }
                first = false;
                json += L'"';
                for (wchar_t c : value)
                {
                    switch (c)
                    {
                    case L'"': json += L"\\\""; break;
                    case L'\\': json += L"\\\\"; break;
                    case L'\n': json += L"\\n"; break;
                    case L'\r': json += L"\\r"; break;
                    case L'\t': json += L"\\t"; break;
                    case L'\b': json += L"\\b"; break;
                    case L'\f': json += L"\\f"; break;
                    default:
                        if (static_cast<std::uint32_t>(c) < 0x20)
                        {
                            const auto code = static_cast<std::uint32_t>(c);
                            json += L"\\u00";
                            json += kHex[(code >> 4) & 0xF];
                            json += kHex[code & 0xF];
                        }
                        else
                        {
                            json += c;
                        }
                        break;
                    }
                }
                json += L'"';
            }
            json += L']';
            return json;
        }

        class JsonArrayParser
        {
        public:
            explicit JsonArrayParser(std::wstring_view text) : m_text(text) {}

            std::optional<std::vector<std::wstring>> Parse()
            {
                std::vector<std::wstring> result;
                SkipWhitespace();
                if (!Consume(L'['))
                {
                    return std::nullopt;
                }
                SkipWhitespace();
                if (Consume(L']'))
                {
                    return Finish(std::move(result));
                }
                while (true)
                {
                    SkipWhitespace();
                    auto value = ParseString();
                    if (!value)
                    {
                        return std::nullopt;
                    }
                    result.push_back(std::move(*value));
                    SkipWhitespace();
                    if (Consume(L','))
                    {
                        continue;
                    }
                    if (Consume(L']'))
                    {
                        return Finish(std::move(result));
                    }
                    return std::nullopt;
                }
            }

        private:
            std::optional<std::vector<std::wstring>> Finish(std::vector<std::wstring>&& result)
            {
                SkipWhitespace();
                if (m_pos != m_text.size())
                {
                    return std::nullopt;
                }
                return std::move(result);
            }

            void SkipWhitespace() noexcept
            {
                while (m_pos < m_text.size() &&
                       (m_text[m_pos] == L' ' || m_text[m_pos] == L'\t' || m_text[m_pos] == L'\n' || m_text[m_pos] == L'\r'))
                {
                    ++m_pos;
                }
            }

            bool Consume(wchar_t c) noexcept
            {
                if (m_pos < m_text.size() && m_text[m_pos] == c)
                {
                    ++m_pos;
                    return true;
                }
                return false;
            }

            static int HexValue(wchar_t c) noexcept
            {
                if (c >= L'0' && c <= L'9') return c - L'0';
                if (c >= L'a' && c <= L'f') return c - L'a' + 10;
                if (c >= L'A' && c <= L'F') return c - L'A' + 10;
                return -1;
            }

            std::optional<std::wstring> ParseString()
            {
                if (!Consume(L'"'))
                {
                    return std::nullopt;
                }
                std::wstring value;
                while (m_pos < m_text.size())
                {
                    const wchar_t c = m_text[m_pos++];
                    if (c == L'"')
                    {
                        return value;
                    }
                    if (c != L'\\')
                    {
                        value += c;
                        continue;
                    }
                    if (m_pos >= m_text.size())
                    {
                        return std::nullopt;
                    }
                    const wchar_t escape = m_text[m_pos++];
                    switch (escape)
                    {
                    case L'"': value += L'"'; break;
                    case L'\\': value += L'\\'; break;
                    case L'/': value += L'/'; break;
                    case L'b': value += L'\b'; break;
                    case L'f': value += L'\f'; break;
                    case L'n': value += L'\n'; break;
                    case L'r': value += L'\r'; break;
                    case L't': value += L'\t'; break;
                    case L'u':
                    {
                        if (m_pos + 4 > m_text.size())
                        {
                            return std::nullopt;
                        }
                        std::uint32_t code = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            const int digit = HexValue(m_text[m_pos++]);
                            if (digit < 0)
                            {
                                return std::nullopt;
                            }
                            code = (code << 4) | static_cast<std::uint32_t>(digit);
                        }
                        value += static_cast<wchar_t>(code);
                        break;
                    }
                    default:
                        return std::nullopt;
                    }
                }
                return std::nullopt;   // unterminated
            }

            std::wstring_view m_text;
            std::size_t m_pos = 0;
        };

        bool SameList(std::vector<std::wstring> const& a, std::vector<std::wstring> const& b)
        {
            return a == b;
        }

        // Last-resort backend, used only if CreateDefaultSettingsStore() returns null.
        class MemorySettingsStore final : public SettingsStore
        {
        public:
            std::optional<std::wstring> Read(std::wstring_view key) const override
            {
                for (auto const& [k, v] : m_values)
                {
                    if (k == key)
                    {
                        return v;
                    }
                }
                return std::nullopt;
            }
            void Write(std::wstring_view key, std::wstring_view value) override
            {
                for (auto& [k, v] : m_values)
                {
                    if (k == key)
                    {
                        v = value;
                        return;
                    }
                }
                m_values.emplace_back(std::wstring{ key }, std::wstring{ value });
            }
            void Remove(std::wstring_view key) override
            {
                std::erase_if(m_values, [key](auto const& entry) { return entry.first == key; });
            }

        private:
            std::vector<std::pair<std::wstring, std::wstring>> m_values;
        };
    }

    // ---- infrastructure ----

    Settings& Settings::Instance()
    {
        static Settings instance;
        return instance;
    }

    void Settings::UseStore(std::unique_ptr<SettingsStore> store)
    {
        auto& settings = Instance();
        settings.m_store = std::move(store);
        settings.m_cache.clear();
    }

    SettingsStore& Settings::Store() const
    {
        if (!m_store)
        {
            m_store = CreateDefaultSettingsStore();
            if (!m_store)
            {
                m_store = std::make_unique<MemorySettingsStore>();
            }
        }
        return *m_store;
    }

    std::optional<std::wstring> Settings::ReadRaw(std::wstring_view key) const
    {
        for (auto const& [cachedKey, cachedValue] : m_cache)
        {
            if (cachedKey == key)
            {
                return cachedValue;
            }
        }
        std::optional<std::wstring> value;
        try
        {
            value = Store().Read(key);
        }
        catch (...)
        {
            value.reset();
        }
        m_cache.emplace_back(std::wstring{ key }, value);
        return value;
    }

    void Settings::WriteRaw(std::wstring_view key, std::wstring const& value, std::wstring const& current)
    {
        if (value == current)
        {
            return;
        }
        bool cached = false;
        for (auto& [cachedKey, cachedValue] : m_cache)
        {
            if (cachedKey == key)
            {
                cachedValue = value;
                cached = true;
                break;
            }
        }
        if (!cached)
        {
            m_cache.emplace_back(std::wstring{ key }, value);
        }
        try
        {
            Store().Write(key, value);
        }
        catch (...)
        {
            // Keep the in-memory value; persistence failures are not fatal.
        }
        Changed(key);
    }

    void Settings::RemoveRaw(std::wstring_view key)
    {
        bool cached = false;
        for (auto& [cachedKey, cachedValue] : m_cache)
        {
            if (cachedKey == key)
            {
                cachedValue.reset();
                cached = true;
                break;
            }
        }
        if (!cached)
        {
            m_cache.emplace_back(std::wstring{ key }, std::nullopt);
        }
        try
        {
            Store().Remove(key);
        }
        catch (...)
        {
        }
    }

    // ---- typed helpers ----

    bool Settings::GetBool(std::wstring_view key, bool fallback) const
    {
        if (auto raw = ReadRaw(key))
        {
            if (auto value = ParseBool(*raw))
            {
                return *value;
            }
        }
        return fallback;
    }

    void Settings::SetBool(std::wstring_view key, bool value, bool fallback)
    {
        WriteRaw(key, FormatBool(value), FormatBool(GetBool(key, fallback)));
    }

    std::int32_t Settings::GetInt(std::wstring_view key, std::int32_t fallback, std::int32_t minimum, std::int32_t maximum) const
    {
        if (auto raw = ReadRaw(key))
        {
            if (auto value = ParseInt(*raw))
            {
                return std::clamp(*value, minimum, maximum);
            }
        }
        return fallback;
    }

    void Settings::SetInt(std::wstring_view key, std::int32_t value, std::int32_t fallback, std::int32_t minimum, std::int32_t maximum)
    {
        value = std::clamp(value, minimum, maximum);
        WriteRaw(key, FormatInt(value), FormatInt(GetInt(key, fallback, minimum, maximum)));
    }

    std::int32_t Settings::GetEnum(std::wstring_view key, std::int32_t fallback, std::int32_t maximum) const
    {
        if (auto raw = ReadRaw(key))
        {
            if (auto value = ParseInt(*raw); value && *value >= 0 && *value <= maximum)
            {
                return *value;
            }
        }
        return fallback;
    }

    double Settings::GetDouble(std::wstring_view key, double fallback) const
    {
        if (auto raw = ReadRaw(key))
        {
            if (auto value = ParseDouble(*raw))
            {
                return *value;
            }
        }
        return fallback;
    }

    void Settings::SetDouble(std::wstring_view key, double value, double fallback)
    {
        if (!std::isfinite(value))
        {
            return;
        }
        WriteRaw(key, FormatDouble(value), FormatDouble(GetDouble(key, fallback)));
    }

    KeyShortcut Settings::GetShortcut(std::wstring_view key, KeyShortcut fallback) const
    {
        if (auto raw = ReadRaw(key))
        {
            if (auto value = KeyShortcut::Parse(*raw))
            {
                return *value;
            }
        }
        return fallback;
    }

    void Settings::SetShortcut(std::wstring_view key, KeyShortcut value, KeyShortcut fallback)
    {
        if (value.IsEmpty())
        {
            value = KeyShortcut{};   // normalize "no key" to the empty shortcut
        }
        const KeyShortcut current = GetShortcut(key, fallback);
        if (current == value)
        {
            return;
        }
        // ToString is injective, so different shortcuts always serialize differently.
        WriteRaw(key, value.ToString(), current.ToString());
    }

    std::vector<std::wstring> Settings::GetList(std::wstring_view key, std::vector<std::wstring> const& fallback) const
    {
        if (auto raw = ReadRaw(key))
        {
            if (auto value = JsonArrayParser(*raw).Parse())
            {
                return std::move(*value);
            }
        }
        return fallback;
    }

    void Settings::SetList(std::wstring_view key, std::vector<std::wstring> const& value, std::vector<std::wstring> const& fallback)
    {
        const auto current = GetList(key, fallback);
        if (SameList(current, value))
        {
            return;
        }
        WriteRaw(key, EncodeJsonStringArray(value), EncodeJsonStringArray(current));
    }

    namespace
    {
        constexpr KeyShortcut kDefaultPopupShortcut{ Modifiers::Win | Modifiers::Shift, 'C' };
        constexpr KeyShortcut kDefaultPinShortcut{ Modifiers::Alt, 'P' };
        constexpr KeyShortcut kDefaultDeleteShortcut{ Modifiers::Alt, VK_DELETE };
        constexpr KeyShortcut kDefaultTogglePreviewShortcut{ Modifiers::Ctrl, VK_SPACE };

        constexpr std::int32_t kMaxInt = (std::numeric_limits<std::int32_t>::max)();

        std::vector<std::wstring> DefaultIgnoredFormats()
        {
            return { L"Kleio.Transient" };
        }
    }

    // ---- General ----
    bool Settings::CheckForUpdates() const { return GetBool(kCheckForUpdates, true); }
    void Settings::CheckForUpdates(bool value) { SetBool(kCheckForUpdates, value, true); }
    KeyShortcut Settings::PopupShortcut() const { return GetShortcut(kPopupShortcut, kDefaultPopupShortcut); }
    void Settings::PopupShortcut(KeyShortcut value) { SetShortcut(kPopupShortcut, value, kDefaultPopupShortcut); }
    KeyShortcut Settings::PinShortcut() const { return GetShortcut(kPinShortcut, kDefaultPinShortcut); }
    void Settings::PinShortcut(KeyShortcut value) { SetShortcut(kPinShortcut, value, kDefaultPinShortcut); }
    KeyShortcut Settings::DeleteShortcut() const { return GetShortcut(kDeleteShortcut, kDefaultDeleteShortcut); }
    void Settings::DeleteShortcut(KeyShortcut value) { SetShortcut(kDeleteShortcut, value, kDefaultDeleteShortcut); }
    KeyShortcut Settings::TogglePreviewShortcut() const { return GetShortcut(kTogglePreviewShortcut, kDefaultTogglePreviewShortcut); }
    void Settings::TogglePreviewShortcut(KeyShortcut value) { SetShortcut(kTogglePreviewShortcut, value, kDefaultTogglePreviewShortcut); }
    Kleio::SearchMode Settings::SearchMode() const
    {
        return static_cast<Kleio::SearchMode>(GetEnum(kSearchMode, 0, 3));
    }
    void Settings::SearchMode(Kleio::SearchMode value)
    {
        const auto raw = static_cast<std::int32_t>(value);
        if (raw < 0 || raw > 3) return;
        WriteRaw(kSearchMode, FormatInt(raw), FormatInt(GetEnum(kSearchMode, 0, 3)));
    }
    bool Settings::PasteByDefault() const { return GetBool(kPasteByDefault, false); }
    void Settings::PasteByDefault(bool value) { SetBool(kPasteByDefault, value, false); }
    bool Settings::RemoveFormattingByDefault() const { return GetBool(kRemoveFormattingByDefault, false); }
    void Settings::RemoveFormattingByDefault(bool value) { SetBool(kRemoveFormattingByDefault, value, false); }
    bool Settings::ReplaceWinV() const { return GetBool(kReplaceWinV, false); }
    void Settings::ReplaceWinV(bool value) { SetBool(kReplaceWinV, value, false); }
    bool Settings::BlockWinC() const { return GetBool(kBlockWinC, false); }
    void Settings::BlockWinC(bool value) { SetBool(kBlockWinC, value, false); }
    bool Settings::PlaySounds() const { return GetBool(kPlaySounds, false); }
    void Settings::PlaySounds(bool value) { SetBool(kPlaySounds, value, false); }
    bool Settings::ShowNotifications() const { return GetBool(kShowNotifications, false); }
    void Settings::ShowNotifications(bool value) { SetBool(kShowNotifications, value, false); }

    // ---- Storage ----
    bool Settings::SaveFiles() const { return GetBool(kSaveFiles, true); }
    void Settings::SaveFiles(bool value) { SetBool(kSaveFiles, value, true); }
    bool Settings::SaveImages() const { return GetBool(kSaveImages, true); }
    void Settings::SaveImages(bool value) { SetBool(kSaveImages, value, true); }
    bool Settings::SaveText() const { return GetBool(kSaveText, true); }
    void Settings::SaveText(bool value) { SetBool(kSaveText, value, true); }
    std::int32_t Settings::HistorySize() const { return GetInt(kHistorySize, 200, 1, 999); }
    void Settings::HistorySize(std::int32_t value) { SetInt(kHistorySize, value, 200, 1, 999); }
    Kleio::SortBy Settings::SortBy() const
    {
        return static_cast<Kleio::SortBy>(GetEnum(kSortBy, 0, 2));
    }
    void Settings::SortBy(Kleio::SortBy value)
    {
        const auto raw = static_cast<std::int32_t>(value);
        if (raw < 0 || raw > 2) return;
        WriteRaw(kSortBy, FormatInt(raw), FormatInt(GetEnum(kSortBy, 0, 2)));
    }

    // ---- Appearance ----
    Kleio::PopupPosition Settings::PopupPosition() const
    {
        return static_cast<Kleio::PopupPosition>(GetEnum(kPopupPosition, 0, 4));
    }
    void Settings::PopupPosition(Kleio::PopupPosition value)
    {
        const auto raw = static_cast<std::int32_t>(value);
        if (raw < 0 || raw > 4) return;
        WriteRaw(kPopupPosition, FormatInt(raw), FormatInt(GetEnum(kPopupPosition, 0, 4)));
    }
    std::int32_t Settings::PopupScreen() const { return GetInt(kPopupScreen, 0, 0, kMaxInt); }
    void Settings::PopupScreen(std::int32_t value) { SetInt(kPopupScreen, value, 0, 0, kMaxInt); }
    PinsPosition Settings::PinTo() const
    {
        return static_cast<PinsPosition>(GetEnum(kPinTo, 0, 1));
    }
    void Settings::PinTo(PinsPosition value)
    {
        const auto raw = static_cast<std::int32_t>(value);
        if (raw < 0 || raw > 1) return;
        WriteRaw(kPinTo, FormatInt(raw), FormatInt(GetEnum(kPinTo, 0, 1)));
    }
    std::int32_t Settings::ImageMaxHeight() const { return GetInt(kImageMaxHeight, 40, 1, 200); }
    void Settings::ImageMaxHeight(std::int32_t value) { SetInt(kImageMaxHeight, value, 40, 1, 200); }
    bool Settings::OpenPreviewAutomatically() const { return GetBool(kOpenPreviewAutomatically, true); }
    void Settings::OpenPreviewAutomatically(bool value) { SetBool(kOpenPreviewAutomatically, value, true); }
    std::int32_t Settings::PreviewDelay() const { return GetInt(kPreviewDelay, 1500, 200, 100000); }
    void Settings::PreviewDelay(std::int32_t value) { SetInt(kPreviewDelay, value, 1500, 200, 100000); }
    Kleio::HighlightMatch Settings::HighlightMatch() const
    {
        return static_cast<Kleio::HighlightMatch>(GetEnum(kHighlightMatch, 0, 4));
    }
    void Settings::HighlightMatch(Kleio::HighlightMatch value)
    {
        const auto raw = static_cast<std::int32_t>(value);
        if (raw < 0 || raw > 4) return;
        WriteRaw(kHighlightMatch, FormatInt(raw), FormatInt(GetEnum(kHighlightMatch, 0, 4)));
    }
    bool Settings::ShowSpecialSymbols() const { return GetBool(kShowSpecialSymbols, true); }
    void Settings::ShowSpecialSymbols(bool value) { SetBool(kShowSpecialSymbols, value, true); }
    bool Settings::ShowTrayIcon() const { return GetBool(kShowTrayIcon, true); }
    void Settings::ShowTrayIcon(bool value) { SetBool(kShowTrayIcon, value, true); }
    TrayIconStyle Settings::TrayIcon() const
    {
        return static_cast<TrayIconStyle>(GetEnum(kTrayIcon, 0, 3));
    }
    void Settings::TrayIcon(TrayIconStyle value)
    {
        const auto raw = static_cast<std::int32_t>(value);
        if (raw < 0 || raw > 3) return;
        WriteRaw(kTrayIcon, FormatInt(raw), FormatInt(GetEnum(kTrayIcon, 0, 3)));
    }
    bool Settings::ShowRecentCopyInTray() const { return GetBool(kShowRecentCopyInTray, false); }
    void Settings::ShowRecentCopyInTray(bool value) { SetBool(kShowRecentCopyInTray, value, false); }
    bool Settings::ShowSearch() const { return GetBool(kShowSearch, true); }
    void Settings::ShowSearch(bool value) { SetBool(kShowSearch, value, true); }
    Kleio::SearchVisibility Settings::SearchVisibility() const
    {
        return static_cast<Kleio::SearchVisibility>(GetEnum(kSearchVisibility, 0, 1));
    }
    void Settings::SearchVisibility(Kleio::SearchVisibility value)
    {
        const auto raw = static_cast<std::int32_t>(value);
        if (raw < 0 || raw > 1) return;
        WriteRaw(kSearchVisibility, FormatInt(raw), FormatInt(GetEnum(kSearchVisibility, 0, 1)));
    }
    bool Settings::ShowTitle() const { return GetBool(kShowTitle, true); }
    void Settings::ShowTitle(bool value) { SetBool(kShowTitle, value, true); }
    bool Settings::ShowApplicationIcons() const { return GetBool(kShowApplicationIcons, false); }
    void Settings::ShowApplicationIcons(bool value) { SetBool(kShowApplicationIcons, value, false); }
    bool Settings::ShowHexColorSwatch() const { return GetBool(kShowHexColorSwatch, true); }
    void Settings::ShowHexColorSwatch(bool value) { SetBool(kShowHexColorSwatch, value, true); }
    bool Settings::ShowFooter() const { return GetBool(kShowFooter, true); }
    void Settings::ShowFooter(bool value) { SetBool(kShowFooter, value, true); }
    double Settings::WindowWidth() const { return GetDouble(kWindowWidth, 450.0); }
    void Settings::WindowWidth(double value) { SetDouble(kWindowWidth, value, 450.0); }
    double Settings::WindowHeight() const { return GetDouble(kWindowHeight, 800.0); }
    void Settings::WindowHeight(double value) { SetDouble(kWindowHeight, value, 800.0); }
    double Settings::WindowPositionX() const { return GetDouble(kWindowPositionX, 0.5); }
    void Settings::WindowPositionX(double value) { SetDouble(kWindowPositionX, value, 0.5); }
    double Settings::WindowPositionY() const { return GetDouble(kWindowPositionY, 0.2); }
    void Settings::WindowPositionY(double value) { SetDouble(kWindowPositionY, value, 0.2); }
    void Settings::ResetWindowPosition()
    {
        WindowPositionX(0.5);
        WindowPositionY(0.2);
        RemoveRaw(kWindowPositionX);
        RemoveRaw(kWindowPositionY);
    }
    double Settings::PreviewWidth() const { return GetDouble(kPreviewWidth, 400.0); }
    void Settings::PreviewWidth(double value) { SetDouble(kPreviewWidth, value, 400.0); }

    // ---- Ignore ----
    std::vector<std::wstring> Settings::IgnoredApps() const { return GetList(kIgnoredApps, {}); }
    void Settings::IgnoredApps(std::vector<std::wstring> const& value) { SetList(kIgnoredApps, value, {}); }
    bool Settings::IgnoreAllAppsExceptListed() const { return GetBool(kIgnoreAllAppsExceptListed, false); }
    void Settings::IgnoreAllAppsExceptListed(bool value) { SetBool(kIgnoreAllAppsExceptListed, value, false); }
    std::vector<std::wstring> Settings::IgnoredFormats() const { return GetList(kIgnoredFormats, DefaultIgnoredFormats()); }
    void Settings::IgnoredFormats(std::vector<std::wstring> const& value) { SetList(kIgnoredFormats, value, DefaultIgnoredFormats()); }
    std::vector<std::wstring> Settings::IgnoreRegexps() const { return GetList(kIgnoreRegexps, {}); }
    void Settings::IgnoreRegexps(std::vector<std::wstring> const& value) { SetList(kIgnoreRegexps, value, {}); }

    // ---- Advanced ----
    bool Settings::IgnoreEvents() const { return GetBool(kIgnoreEvents, false); }
    void Settings::IgnoreEvents(bool value) { SetBool(kIgnoreEvents, value, false); }
    bool Settings::IgnoreOnlyNextEvent() const { return GetBool(kIgnoreOnlyNextEvent, false); }
    void Settings::IgnoreOnlyNextEvent(bool value) { SetBool(kIgnoreOnlyNextEvent, value, false); }
    bool Settings::ClearOnQuit() const { return GetBool(kClearOnQuit, false); }
    void Settings::ClearOnQuit(bool value) { SetBool(kClearOnQuit, value, false); }
    bool Settings::ClearSystemClipboard() const { return GetBool(kClearSystemClipboard, false); }
    void Settings::ClearSystemClipboard(bool value) { SetBool(kClearSystemClipboard, value, false); }

    // ---- Internal state ----
    bool Settings::SuppressClearAlert() const { return GetBool(kSuppressClearAlert, false); }
    void Settings::SuppressClearAlert(bool value) { SetBool(kSuppressClearAlert, value, false); }
    std::vector<std::wstring> Settings::PinOrder() const { return GetList(kPinOrder, {}); }
    void Settings::PinOrder(std::vector<std::wstring> const& value) { SetList(kPinOrder, value, {}); }
    double Settings::LastUpdateCheck() const { return GetDouble(kLastUpdateCheck, 0.0); }
    void Settings::LastUpdateCheck(double value) { SetDouble(kLastUpdateCheck, value, 0.0); }
    std::wstring Settings::SkippedUpdateVersion() const { return ReadRaw(kSkippedUpdateVersion).value_or(std::wstring{}); }
    void Settings::SkippedUpdateVersion(std::wstring const& value) { WriteRaw(kSkippedUpdateVersion, value, SkippedUpdateVersion()); }

    std::wstring Settings::Language() const { return ReadRaw(kLanguage).value_or(std::wstring{}); }
    void Settings::Language(std::wstring const& value) { WriteRaw(kLanguage, value, Language()); }
}
