#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Kleio::ClipboardFormats
{
    // How a clipboard format is captured into / restored from a history item. Port of the type
    // filtering in Maccy's Clipboard.checkForChangesInPasteboard (supportedTypes, disabledTypes,
    // isPersistable) adapted to Windows clipboard formats.
    enum class Kind
    {
        // Not stored: synthesized by Windows from another stored format (CF_TEXT, CF_OEMTEXT,
        // CF_LOCALE, CF_BITMAP, CF_DIB, CF_DIBV5, CF_METAFILEPICT, CF_PALETTE), not plain memory
        // (CF_OWNERDISPLAY, CF_DSP*, private and GDI object ranges), OLE plumbing that only makes
        // sense while the source app owns the clipboard (Maccy's "com.microsoft.ole.source.*"),
        // shell drag-and-drop / paste negotiation state, and Kleio's own markers.
        Skip,
        Text,       // CF_UNICODETEXT -> ContentType::Text (Maccy's .string)
        Html,       // "HTML Format" -> ContentType::Html (.html)
        Rtf,        // "Rich Text Format" -> ContentType::Rtf (.rtf)
        Files,      // CF_HDROP -> ContentType::Files (.fileURL)
        Png,        // "PNG" -> ContentType::Png (.png)
        Bitmap,     // CF_DIBV5 / CF_DIB: converted to ContentType::Png when there is no "PNG"
        Image,      // other image encodings (CF_TIFF, "JFIF", "GIF", "image/..."): raw bytes, Maccy's .tiff/.jpeg/.heic
        Metafile,   // CF_ENHMETAFILE: stored as GetEnhMetaFileBits bytes under "CF_ENHMETAFILE"
        Raw,        // any other HGLOBAL format: raw bytes under its name
    };

    // Which "Save ..." setting (Maccy's enabledPasteboardTypes / StorageType) a kind belongs to.
    enum class SaveSetting { None, Text, Files, Images };

    // Name under which a format is stored in HistoryItemContent::type: "CF_*" for the predefined
    // formats, GetClipboardFormatName for registered ones. Returns "" for formats without a name
    // (unknown predefined ids, private / GDI object ranges).
    std::wstring StandardFormatName(unsigned int id);
    // Inverse of StandardFormatName for the predefined formats; 0 for any other name.
    unsigned int StandardFormatId(std::wstring_view name) noexcept;

    // Classifies a format present on the clipboard. `name` is the stored name (see above).
    Kind Classify(unsigned int id, std::wstring_view name) noexcept;
    // Classifies a stored content type (used when restoring and when cleaning up the database).
    Kind ClassifyStored(std::wstring_view type) noexcept;

    SaveSetting SaveSettingOf(Kind kind) noexcept;

    // Maccy's Clipboard.isPersistable for a stored content type name: false for formats Kleio
    // never stores (see Kind::Skip). Storage::RemoveUnpersistableContents drops such rows.
    bool IsPersistable(std::wstring_view type) noexcept;

    // Largest single format stored (SQLite's default SQLITE_MAX_LENGTH is 1e9 bytes).
    inline constexpr std::size_t MaxFormatSize = 512u * 1024u * 1024u;

    // Name used for CF_ENHMETAFILE contents (GetEnhMetaFileBits bytes).
    inline constexpr std::wstring_view EnhMetafile = L"CF_ENHMETAFILE";
}
