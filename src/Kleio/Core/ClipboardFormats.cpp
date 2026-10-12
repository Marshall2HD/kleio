#include "pch.h"
#include "Core/ClipboardFormats.h"
#include "Core/HistoryItem.h"

namespace Kleio::ClipboardFormats
{
    namespace
    {
        struct StandardFormat
        {
            unsigned int id;
            std::wstring_view name;
        };

        constexpr StandardFormat kStandardFormats[] = {
            { CF_TEXT, L"CF_TEXT" },
            { CF_BITMAP, L"CF_BITMAP" },
            { CF_METAFILEPICT, L"CF_METAFILEPICT" },
            { CF_SYLK, L"CF_SYLK" },
            { CF_DIF, L"CF_DIF" },
            { CF_TIFF, L"CF_TIFF" },
            { CF_OEMTEXT, L"CF_OEMTEXT" },
            { CF_DIB, L"CF_DIB" },
            { CF_PALETTE, L"CF_PALETTE" },
            { CF_PENDATA, L"CF_PENDATA" },
            { CF_RIFF, L"CF_RIFF" },
            { CF_WAVE, L"CF_WAVE" },
            { CF_UNICODETEXT, L"CF_UNICODETEXT" },
            { CF_ENHMETAFILE, L"CF_ENHMETAFILE" },
            { CF_HDROP, L"CF_HDROP" },
            { CF_LOCALE, L"CF_LOCALE" },
            { CF_DIBV5, L"CF_DIBV5" },
            { CF_OWNERDISPLAY, L"CF_OWNERDISPLAY" },
            { CF_DSPTEXT, L"CF_DSPTEXT" },
            { CF_DSPBITMAP, L"CF_DSPBITMAP" },
            { CF_DSPMETAFILEPICT, L"CF_DSPMETAFILEPICT" },
            { CF_DSPENHMETAFILE, L"CF_DSPENHMETAFILE" },
        };

        // OLE plumbing: references to the live data object / embedding of the source app. They
        // are meaningless once the source app no longer owns the clipboard (the Windows analogue
        // of Maccy's "com.microsoft.ole.source." prefix and its Word link-source exclusion).
        constexpr std::wstring_view kOleFormats[] = {
            L"Ole Private Data",
            L"DataObject",
            L"Object Descriptor",
            L"Link Source Descriptor",
            L"Link Source",
            L"Custom Link Source",
            L"Embed Source",
            L"Embedded Object",
            L"OwnerLink",
            L"ObjectLink",
            L"Native",
            L"Link",
            L"OleClipboardPersistOnFlush",
        };

        // Shell drag-and-drop / paste negotiation state (CFSTR_* in shlobj.h). "Preferred
        // DropEffect" is written by Kleio itself (always "copy") next to CF_HDROP, so a restored
        // Explorer cut never moves files a second time. "FileContents" is usually an IStream and
        // only addressable per index.
        constexpr std::wstring_view kShellStateFormats[] = {
            L"Preferred DropEffect",
            L"Performed DropEffect",
            L"Logical Performed DropEffect",
            L"Paste Succeeded",
            L"Shell Object Offsets",
            L"TargetCLSID",
            L"InShellDragLoop",
            L"DragContext",
            L"DragImageBits",
            L"DragWindow",
            L"DragSourceHelperFlags",
            L"UsingDefaultDragImage",
            L"IsShowingLayered",
            L"IsShowingText",
            L"IsComputingImage",
            L"DisableDragText",
            L"DropDescription",
            L"ComputedDragImage",
            L"DataObjectAttributes",
            L"DataObjectAttributesRequiringElevation",
            L"UIDisplayed",
            L"AsyncFlag",
            L"UntrustedDragDrop",
            L"FileContents",
        };

        // Image encodings other than PNG / DIB (Maccy's StorageType.images: png, tiff, jpeg, heic).
        constexpr std::wstring_view kImageFormats[] = {
            L"JFIF",
            L"GIF",
            L"image/png",
            L"image/jpeg",
            L"image/gif",
            L"image/tiff",
            L"image/bmp",
            L"image/webp",
            L"image/heic",
            L"image/heif",
            L"image/avif",
        };

        wchar_t AsciiLower(wchar_t c) noexcept
        {
            return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
        }

        // Registered clipboard format names are case-insensitive.
        bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept
        {
            if (a.size() != b.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                if (AsciiLower(a[i]) != AsciiLower(b[i]))
                {
                    return false;
                }
            }
            return true;
        }

        bool StartsWithIgnoreCase(std::wstring_view text, std::wstring_view prefix) noexcept
        {
            return text.size() >= prefix.size() && EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
        }

        template <std::size_t N>
        bool Contains(std::wstring_view const (&list)[N], std::wstring_view name) noexcept
        {
            for (auto entry : list)
            {
                if (EqualsIgnoreCase(entry, name))
                {
                    return true;
                }
            }
            return false;
        }

        Kind ClassifyRegistered(std::wstring_view name) noexcept
        {
            if (name.empty() || StartsWithIgnoreCase(name, L"Kleio.") ||
                EqualsIgnoreCase(name, ContentType::CanIncludeInClipboardHistory))
            {
                return Kind::Skip;   // Kleio's markers; the history opt-out is checked, never stored
            }
            if (EqualsIgnoreCase(name, ContentType::Html))
            {
                return Kind::Html;
            }
            if (EqualsIgnoreCase(name, ContentType::Rtf))
            {
                return Kind::Rtf;
            }
            if (EqualsIgnoreCase(name, ContentType::Png))
            {
                return Kind::Png;
            }
            if (Contains(kOleFormats, name) || Contains(kShellStateFormats, name))
            {
                return Kind::Skip;
            }
            if (Contains(kImageFormats, name))
            {
                return Kind::Image;
            }
            return Kind::Raw;
        }
    }

    std::wstring StandardFormatName(unsigned int id)
    {
        for (auto const& format : kStandardFormats)
        {
            if (format.id == id)
            {
                return std::wstring{ format.name };
            }
        }
        return {};
    }

    unsigned int StandardFormatId(std::wstring_view name) noexcept
    {
        for (auto const& format : kStandardFormats)
        {
            if (format.name == name)
            {
                return format.id;
            }
        }
        return 0;
    }

    Kind Classify(unsigned int id, std::wstring_view name) noexcept
    {
        if (id >= 0xC000 && id <= 0xFFFF)
        {
            return ClassifyRegistered(name);
        }
        switch (id)
        {
        case CF_UNICODETEXT:
            return Kind::Text;
        case CF_HDROP:
            return Kind::Files;
        case CF_DIB:
        case CF_DIBV5:
            return Kind::Bitmap;
        case CF_TIFF:
            return Kind::Image;
        case CF_ENHMETAFILE:
            return Kind::Metafile;
        case CF_SYLK:
        case CF_DIF:
        case CF_PENDATA:
        case CF_RIFF:
        case CF_WAVE:
            return Kind::Raw;
        default:
            // CF_TEXT / CF_OEMTEXT / CF_LOCALE (synthesized from CF_UNICODETEXT), CF_BITMAP /
            // CF_PALETTE (GDI handles, synthesized from CF_DIBV5), CF_METAFILEPICT (synthesized from
            // CF_ENHMETAFILE), CF_OWNERDISPLAY / CF_DSP* (owner-private), CF_PRIVATEFIRST..LAST and
            // CF_GDIOBJFIRST..LAST (handles of unknown kind) and unknown predefined ids.
            return Kind::Skip;
        }
    }

    Kind ClassifyStored(std::wstring_view type) noexcept
    {
        if (const unsigned int id = StandardFormatId(type); id != 0)
        {
            return Classify(id, type);
        }
        if (type.starts_with(L"CF_"))
        {
            return Kind::Skip;
        }
        return ClassifyRegistered(type);
    }

    SaveSetting SaveSettingOf(Kind kind) noexcept
    {
        switch (kind)
        {
        case Kind::Text:
        case Kind::Html:
        case Kind::Rtf:
            return SaveSetting::Text;
        case Kind::Files:
            return SaveSetting::Files;
        case Kind::Png:
        case Kind::Bitmap:
        case Kind::Image:
            return SaveSetting::Images;
        default:
            return SaveSetting::None;
        }
    }

    bool IsPersistable(std::wstring_view type) noexcept
    {
        if (type == ContentType::FromKleio)
        {
            return true;   // stored marker (Maccy keeps "org.p0deje.Maccy" too)
        }
        const Kind kind = ClassifyStored(type);
        return kind != Kind::Skip && kind != Kind::Bitmap;
    }
}
