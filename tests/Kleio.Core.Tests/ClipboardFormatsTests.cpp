#include "pch.h"
#include "TestFramework.h"

#include "Core/ClipboardFormats.h"
#include "Core/HistoryItem.h"

using namespace Kleio;
using ClipboardFormats::Kind;

TEST(ClipboardFormats_StandardNames)
{
    CHECK_EQ(ClipboardFormats::StandardFormatName(CF_UNICODETEXT), std::wstring{ ContentType::Text });
    CHECK_EQ(ClipboardFormats::StandardFormatName(CF_HDROP), std::wstring{ ContentType::Files });
    CHECK_EQ(ClipboardFormats::StandardFormatName(CF_ENHMETAFILE), std::wstring{ ClipboardFormats::EnhMetafile });
    CHECK_EQ(ClipboardFormats::StandardFormatName(0x0250), std::wstring{});   // private range
    for (unsigned int id : std::initializer_list<unsigned int>{ CF_TEXT, CF_BITMAP, CF_DIB, CF_DIBV5, CF_TIFF, CF_RIFF, CF_WAVE, CF_LOCALE })
    {
        CHECK(ClipboardFormats::StandardFormatId(ClipboardFormats::StandardFormatName(id)) == id);
    }
    CHECK(ClipboardFormats::StandardFormatId(L"HTML Format") == 0u);
    CHECK(ClipboardFormats::StandardFormatId(L"cf_text") == 0u);   // stored names are exact
}

TEST(ClipboardFormats_ClassifyPredefined)
{
    CHECK(ClipboardFormats::Classify(CF_UNICODETEXT, L"CF_UNICODETEXT") == Kind::Text);
    CHECK(ClipboardFormats::Classify(CF_HDROP, L"CF_HDROP") == Kind::Files);
    CHECK(ClipboardFormats::Classify(CF_DIB, L"CF_DIB") == Kind::Bitmap);
    CHECK(ClipboardFormats::Classify(CF_DIBV5, L"CF_DIBV5") == Kind::Bitmap);
    CHECK(ClipboardFormats::Classify(CF_TIFF, L"CF_TIFF") == Kind::Image);
    CHECK(ClipboardFormats::Classify(CF_ENHMETAFILE, L"CF_ENHMETAFILE") == Kind::Metafile);
    CHECK(ClipboardFormats::Classify(CF_RIFF, L"CF_RIFF") == Kind::Raw);
    CHECK(ClipboardFormats::Classify(CF_WAVE, L"CF_WAVE") == Kind::Raw);
    CHECK(ClipboardFormats::Classify(CF_SYLK, L"CF_SYLK") == Kind::Raw);
    // Synthesized by Windows, GDI handles, owner-private.
    for (unsigned int id : std::initializer_list<unsigned int>{ CF_TEXT, CF_OEMTEXT, CF_LOCALE, CF_BITMAP, CF_PALETTE, CF_METAFILEPICT, CF_OWNERDISPLAY,
                             CF_DSPTEXT, CF_DSPBITMAP, CF_DSPENHMETAFILE, CF_DSPMETAFILEPICT, 0x0200u, 0x02FFu, 0x0300u, 0x03FFu })
    {
        CHECK(ClipboardFormats::Classify(id, ClipboardFormats::StandardFormatName(id)) == Kind::Skip);
    }
}

TEST(ClipboardFormats_ClassifyRegistered)
{
    constexpr unsigned int registered = 0xC100;
    CHECK(ClipboardFormats::Classify(registered, L"HTML Format") == Kind::Html);
    CHECK(ClipboardFormats::Classify(registered, L"Rich Text Format") == Kind::Rtf);
    CHECK(ClipboardFormats::Classify(registered, L"PNG") == Kind::Png);
    CHECK(ClipboardFormats::Classify(registered, L"png") == Kind::Png);   // names are case-insensitive
    CHECK(ClipboardFormats::Classify(registered, L"JFIF") == Kind::Image);
    CHECK(ClipboardFormats::Classify(registered, L"image/svg+xml") == Kind::Raw);
    CHECK(ClipboardFormats::Classify(registered, L"Biff12") == Kind::Raw);
    CHECK(ClipboardFormats::Classify(registered, L"Art::GVML ClipFormat") == Kind::Raw);
    CHECK(ClipboardFormats::Classify(registered, L"Shell IDList Array") == Kind::Raw);
    CHECK(ClipboardFormats::Classify(registered, L"FileNameW") == Kind::Raw);
    CHECK(ClipboardFormats::Classify(registered, L"CanUploadToCloudClipboard") == Kind::Raw);
    CHECK(ClipboardFormats::Classify(registered, L"x.nspasteboard.ModifiedType") == Kind::Raw);
    // OLE plumbing (Maccy's com.microsoft.ole.source.* / Word link sources).
    for (auto name : { L"Ole Private Data", L"DataObject", L"Object Descriptor", L"Link Source", L"Link Source Descriptor",
                       L"Embed Source", L"Embedded Object", L"OwnerLink", L"ObjectLink", L"Native", L"ole private data" })
    {
        CHECK(ClipboardFormats::Classify(registered, name) == Kind::Skip);
    }
    // Shell paste / drag state.
    for (auto name : { L"Preferred DropEffect", L"Performed DropEffect", L"Paste Succeeded", L"Shell Object Offsets",
                       L"DragImageBits", L"FileContents" })
    {
        CHECK(ClipboardFormats::Classify(registered, name) == Kind::Skip);
    }
    // Kleio's markers and the history opt-out.
    CHECK(ClipboardFormats::Classify(registered, L"Kleio.FromKleio") == Kind::Skip);
    CHECK(ClipboardFormats::Classify(registered, L"Kleio.Transient") == Kind::Skip);
    CHECK(ClipboardFormats::Classify(registered, L"CanIncludeInClipboardHistory") == Kind::Skip);
    CHECK(ClipboardFormats::Classify(registered, L"") == Kind::Skip);
}

TEST(ClipboardFormats_ClassifyStoredAndPersistable)
{
    CHECK(ClipboardFormats::ClassifyStored(ContentType::Text) == Kind::Text);
    CHECK(ClipboardFormats::ClassifyStored(ContentType::Files) == Kind::Files);
    CHECK(ClipboardFormats::ClassifyStored(ContentType::Png) == Kind::Png);
    CHECK(ClipboardFormats::ClassifyStored(ContentType::Html) == Kind::Html);
    CHECK(ClipboardFormats::ClassifyStored(L"CF_ENHMETAFILE") == Kind::Metafile);
    CHECK(ClipboardFormats::ClassifyStored(L"CF_WAVE") == Kind::Raw);
    CHECK(ClipboardFormats::ClassifyStored(L"CF_SOMETHING") == Kind::Skip);
    CHECK(ClipboardFormats::ClassifyStored(L"XML Spreadsheet") == Kind::Raw);

    // Everything Kleio stored before all formats were captured stays.
    for (auto type : { ContentType::Text, ContentType::Html, ContentType::Rtf, ContentType::Files, ContentType::Png, ContentType::FromKleio })
    {
        CHECK(ClipboardFormats::IsPersistable(type));
    }
    CHECK(ClipboardFormats::IsPersistable(L"Biff12"));
    CHECK(ClipboardFormats::IsPersistable(L"CF_ENHMETAFILE"));
    CHECK(!ClipboardFormats::IsPersistable(L"Ole Private Data"));
    CHECK(!ClipboardFormats::IsPersistable(L"CF_DIB"));
    CHECK(!ClipboardFormats::IsPersistable(L"CF_BITMAP"));
    CHECK(!ClipboardFormats::IsPersistable(L"CF_TEXT"));
    CHECK(!ClipboardFormats::IsPersistable(L"CanIncludeInClipboardHistory"));
}

TEST(ClipboardFormats_SaveSettings)
{
    using ClipboardFormats::SaveSetting;
    CHECK(ClipboardFormats::SaveSettingOf(Kind::Text) == SaveSetting::Text);
    CHECK(ClipboardFormats::SaveSettingOf(Kind::Html) == SaveSetting::Text);
    CHECK(ClipboardFormats::SaveSettingOf(Kind::Rtf) == SaveSetting::Text);
    CHECK(ClipboardFormats::SaveSettingOf(Kind::Files) == SaveSetting::Files);
    CHECK(ClipboardFormats::SaveSettingOf(Kind::Png) == SaveSetting::Images);
    CHECK(ClipboardFormats::SaveSettingOf(Kind::Bitmap) == SaveSetting::Images);
    CHECK(ClipboardFormats::SaveSettingOf(Kind::Image) == SaveSetting::Images);
    CHECK(ClipboardFormats::SaveSettingOf(Kind::Metafile) == SaveSetting::None);
    CHECK(ClipboardFormats::SaveSettingOf(Kind::Raw) == SaveSetting::None);
}
