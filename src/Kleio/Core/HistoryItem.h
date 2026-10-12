#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Kleio
{
    // Canonical content type names stored in HistoryItemContent::type. Text-ish payloads are stored
    // as raw clipboard bytes:
    //   Text  -> UTF-16LE without terminator (CF_UNICODETEXT)
    //   Html  -> "HTML Format" CF_HTML bytes (UTF-8 with header)
    //   Rtf   -> "Rich Text Format" bytes
    //   Files -> UTF-16LE, absolute paths separated by '\n' (from/to CF_HDROP)
    //   Png   -> PNG encoded image (from "PNG", CF_DIBV5 or CF_DIB)
    // Every other persistable clipboard format is stored as its raw bytes under its format name
    // ("CF_*" for predefined formats, the registered name otherwise; see Core/ClipboardFormats.h).
    // Marker types carry no meaningful payload (may be empty).
    namespace ContentType
    {
        inline constexpr std::wstring_view Text = L"CF_UNICODETEXT";
        inline constexpr std::wstring_view Html = L"HTML Format";
        inline constexpr std::wstring_view Rtf = L"Rich Text Format";
        inline constexpr std::wstring_view Files = L"CF_HDROP";
        inline constexpr std::wstring_view Png = L"PNG";
        // Put on the clipboard by Kleio itself when it copies an item (Maccy's "fromMaccy").
        inline constexpr std::wstring_view FromKleio = L"Kleio.FromKleio";
        // Windows convention: a DWORD 0 under this format means "do not put this into clipboard
        // history"; such copies (from password managers etc.) are ignored. Kleio never writes it.
        inline constexpr std::wstring_view CanIncludeInClipboardHistory = L"CanIncludeInClipboardHistory";
        // Windows convention: DWORD 0 = keep this copy out of the cloud clipboard. Stored and
        // restored, but describes the copy rather than its data (transient).
        inline constexpr std::wstring_view CanUploadToCloudClipboard = L"CanUploadToCloudClipboard";
        // nspasteboard.org "modified" marker (Maccy's .modified): an app that rewrites a copy right
        // after it was made puts the clipboard change count (GetClipboardSequenceNumber) of the
        // original copy here, as decimal text. History merges the rewritten copy into the original.
        inline constexpr std::wstring_view Modified = L"x.nspasteboard.ModifiedType";
    }

    // True for content types that describe how data got onto the clipboard rather than the data
    // itself (Maccy's HistoryItem.transientTypes); they are ignored when looking for duplicates.
    bool IsTransientType(std::wstring_view type) noexcept;

    struct HistoryItemContent
    {
        std::wstring type;
        std::vector<std::uint8_t> value;

        bool operator==(HistoryItemContent const&) const = default;
    };

    // Milliseconds since the Unix epoch (UTC).
    using Timestamp = std::int64_t;
    Timestamp Now() noexcept;

    // One clipboard history entry. Mirrors Maccy's HistoryItem model.
    struct HistoryItem
    {
        std::int64_t id = 0;                 // Storage row id; 0 until inserted
        std::wstring application;            // full path of the source app's executable ("" if unknown)
        Timestamp firstCopiedAt = 0;
        Timestamp lastCopiedAt = 0;
        std::int32_t numberOfCopies = 1;
        std::wstring pin;                    // single lowercase letter when pinned, empty otherwise
        std::wstring title;                  // display title (see GenerateTitle)
        std::vector<HistoryItemContent> contents;

        bool IsPinned() const noexcept { return !pin.empty(); }

        // True when every non-transient content of `other` is also present (same type & bytes) in
        // this item, i.e. `other` is a duplicate that this item should replace.
        bool Supersedes(HistoryItem const& other) const;

        HistoryItemContent const* Find(std::wstring_view type) const noexcept;
        bool Has(std::wstring_view type) const noexcept { return Find(type) != nullptr; }

        std::optional<std::wstring> Text() const;          // decoded CF_UNICODETEXT
        std::optional<std::string> Html() const;           // raw CF_HTML (UTF-8)
        std::optional<std::string> Rtf() const;            // raw RTF
        std::vector<std::wstring> Files() const;           // file paths
        std::vector<std::uint8_t> const* Image() const;    // PNG bytes or nullptr
        bool IsImage() const noexcept { return Image() != nullptr; }
        bool FromKleio() const noexcept { return Has(ContentType::FromKleio); }

        // Plain text used for searching/preview: file paths, else text, else text extracted from
        // RTF/HTML, else the title.
        std::wstring PreviewableText() const;

        // Builds the list title the way Maccy does: first 1000 chars of PreviewableText, with
        // leading/trailing spaces shown as '·', newlines as '⏎' and tabs as '⇥' when
        // showSpecialSymbols is on, otherwise trimmed. Returns "" for images (OCR fills it later).
        std::wstring GenerateTitle(bool showSpecialSymbols) const;

        // When the item's text is a hex colour such as "#ff8800" / "#F80" (optionally with alpha),
        // returns it as 0xAARRGGBB.
        std::optional<std::uint32_t> HexColor() const;
    };

    using HistoryItemPtr = std::shared_ptr<HistoryItem>;

    // Helpers for the content encodings above.
    std::vector<std::uint8_t> EncodeText(std::wstring_view text);
    std::wstring DecodeText(std::vector<std::uint8_t> const& bytes);
    std::vector<std::uint8_t> EncodeFiles(std::vector<std::wstring> const& paths);
    // Extracts readable plain text from RTF / CF_HTML payloads (best effort, no external deps).
    std::wstring PlainTextFromRtf(std::string_view rtf);
    std::wstring PlainTextFromHtml(std::string_view cfHtml);
}
