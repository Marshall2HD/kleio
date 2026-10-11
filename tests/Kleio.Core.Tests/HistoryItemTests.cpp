#include "pch.h"
#include "TestFramework.h"
#include "TestSupport.h"

#include "Core/HistoryItem.h"

#include <cstdio>

using namespace Kleio;
using KleioTests::Bytes;

namespace
{
    HistoryItem TextItem(std::wstring_view text)
    {
        HistoryItem item;
        item.contents.push_back({ std::wstring{ ContentType::Text }, EncodeText(text) });
        return item;
    }

    HistoryItem ItemWith(std::wstring_view type, std::string_view bytes)
    {
        HistoryItem item;
        item.contents.push_back({ std::wstring{ type }, Bytes(bytes) });
        return item;
    }

    // Builds a CF_HTML payload with correct byte offsets.
    std::string MakeCfHtml(std::string const& fragment)
    {
        const std::string prefix = "<html><body>\r\n<!--StartFragment-->";
        const std::string suffix = "<!--EndFragment-->\r\n</body></html>";
        auto header = [](std::size_t startHtml, std::size_t endHtml, std::size_t startFragment, std::size_t endFragment) {
            char buffer[256] = {};
            std::snprintf(buffer, sizeof(buffer),
                          "Version:0.9\r\nStartHTML:%09zu\r\nEndHTML:%09zu\r\nStartFragment:%09zu\r\nEndFragment:%09zu\r\n",
                          startHtml, endHtml, startFragment, endFragment);
            return std::string{ buffer };
        };
        const std::size_t headerLength = header(0, 0, 0, 0).size();
        const std::size_t startHtml = headerLength;
        const std::size_t startFragment = startHtml + prefix.size();
        const std::size_t endFragment = startFragment + fragment.size();
        const std::size_t endHtml = endFragment + suffix.size();
        return header(startHtml, endHtml, startFragment, endFragment) + prefix + fragment + suffix;
    }
}

// ---- titles (ports of Maccy's HistoryItemTests) ----

TEST(HistoryItem_TitleForString)
{
    CHECK_EQ(TextItem(L"foo").GenerateTitle(true), std::wstring{ L"foo" });
}

TEST(HistoryItem_TitleWithWhitespaces)
{
    CHECK_EQ(TextItem(L"   foo bar   ").GenerateTitle(true), std::wstring{ L"\u00B7\u00B7\u00B7foo bar\u00B7\u00B7\u00B7" });
    CHECK_EQ(TextItem(L"   foo bar   ").GenerateTitle(false), std::wstring{ L"foo bar" });
}

TEST(HistoryItem_TitleWithNewlines)
{
    CHECK_EQ(TextItem(L"\nfoo\nbar\n").GenerateTitle(true), std::wstring{ L"\u23CEfoo\u23CEbar\u23CE" });
    CHECK_EQ(TextItem(L"\r\nfoo\r\nbar\r\n").GenerateTitle(true), std::wstring{ L"\u23CEfoo\u23CEbar\u23CE" });
    CHECK_EQ(TextItem(L"\nfoo\nbar\n").GenerateTitle(false), std::wstring{ L"foo\nbar" });
}

TEST(HistoryItem_TitleWithTabs)
{
    CHECK_EQ(TextItem(L"\tfoo\tbar\t").GenerateTitle(true), std::wstring{ L"\u21E5foo\u21E5bar\u21E5" });
}

TEST(HistoryItem_TitleTrailingSpacesBeforeFinalNewline)
{
    // ICU's " +$" also matches right before a final line terminator.
    CHECK_EQ(TextItem(L"foo  \n").GenerateTitle(true), std::wstring{ L"foo\u00B7\u00B7\u23CE" });
    CHECK_EQ(TextItem(L"foo  \r\n").GenerateTitle(true), std::wstring{ L"foo\u00B7\u00B7\u23CE" });
    CHECK_EQ(TextItem(L"a  b").GenerateTitle(true), std::wstring{ L"a  b" });
}

TEST(HistoryItem_TitleWithObjectReplacementCharacters)
{
    CHECK_EQ(TextItem(L"\uFFFCfoo\uFFFCbar\uFFFC").GenerateTitle(true), std::wstring{ L"foobar" });
    CHECK_EQ(TextItem(L"\uFFFC\uFFFC").GenerateTitle(true), std::wstring{});
    CHECK_EQ(TextItem(L"\uFFFC\u0301x").GenerateTitle(true), std::wstring{ L"\u0301x" });
    CHECK_EQ(TextItem(L"\uFFFDfoo").GenerateTitle(true), std::wstring{ L"\uFFFDfoo" });
}

TEST(HistoryItem_TitleIsShortenedTo1000Characters)
{
    const std::wstring longText(1500, L'a');
    CHECK_EQ(TextItem(longText).GenerateTitle(true).size(), std::size_t{ 1000 });
    const std::wstring exact(1000, L'b');
    CHECK_EQ(TextItem(exact).GenerateTitle(true), exact);
}

TEST(HistoryItem_TitleWithRtf)
{
    auto item = ItemWith(ContentType::Rtf,
                         "{\\rtf1\\ansi\\ansicpg1252\\cocoartf2639\n{\\fonttbl\\f0\\fswiss\\fcharset0 Helvetica;}\n"
                         "{\\colortbl;\\red255\\green255\\blue255;}\n{\\*\\expandedcolortbl;;}\n"
                         "\\pard\\tx560\\pardirnatural\\partightenfactor0\n\n\\f0\\fs24 \\cf0 foo}");
    CHECK_EQ(item.GenerateTitle(true), std::wstring{ L"foo" });
}

TEST(HistoryItem_TitleWithHtml)
{
    auto item = ItemWith(ContentType::Html, "<a href='#'>foo</a>");
    CHECK_EQ(item.GenerateTitle(true), std::wstring{ L"foo" });
}

TEST(HistoryItem_TitleForImageIsEmpty)
{
    HistoryItem item = TextItem(L"some text");
    item.contents.push_back({ std::wstring{ ContentType::Png }, Bytes("\x89PNG") });
    CHECK(item.IsImage());
    CHECK_EQ(item.GenerateTitle(true), std::wstring{});
}

TEST(HistoryItem_TitleForFiles)
{
    HistoryItem item;
    item.contents.push_back({ std::wstring{ ContentType::Files }, EncodeFiles({ L"C:\\tmp\\foo.bar", L"C:\\tmp\\\u4EA7\u54C1.txt" }) });
    item.contents.push_back({ std::wstring{ ContentType::Text }, EncodeText(L"ignored") });
    CHECK_EQ(item.GenerateTitle(true), std::wstring{ L"C:\\tmp\\foo.bar\u23CEC:\\tmp\\\u4EA7\u54C1.txt" });
    CHECK_EQ(item.Files(), (std::vector<std::wstring>{ L"C:\\tmp\\foo.bar", L"C:\\tmp\\\u4EA7\u54C1.txt" }));
}

TEST(HistoryItem_TitleWithoutDataFallsBackToTitle)
{
    HistoryItem item;
    CHECK_EQ(item.GenerateTitle(true), std::wstring{});
    item.title = L"OCR text";
    CHECK_EQ(item.PreviewableText(), std::wstring{ L"OCR text" });
}

TEST(HistoryItem_PreviewablePrefersTextOverRichText)
{
    HistoryItem item = TextItem(L"plain");
    item.contents.push_back({ std::wstring{ ContentType::Html }, Bytes("<b>rich</b>") });
    CHECK_EQ(item.PreviewableText(), std::wstring{ L"plain" });

    HistoryItem empty = TextItem(L"");
    empty.contents.push_back({ std::wstring{ ContentType::Rtf }, Bytes("{\\rtf1 rtf text}") });
    empty.contents.push_back({ std::wstring{ ContentType::Html }, Bytes("<b>html</b>") });
    CHECK_EQ(empty.PreviewableText(), std::wstring{ L"rtf text" });
}

// ---- encodings ----

TEST(HistoryItem_TextEncodingRoundTrip)
{
    const std::wstring text = L"Hello, \u4E16\u754C! \u00E9";
    const auto bytes = EncodeText(text);
    CHECK_EQ(bytes.size(), text.size() * 2);
    CHECK_EQ(bytes[0], std::uint8_t{ 'H' });
    CHECK_EQ(bytes[1], std::uint8_t{ 0 });
    CHECK_EQ(DecodeText(bytes), text);

    auto withTerminator = bytes;
    withTerminator.push_back(0);
    withTerminator.push_back(0);
    CHECK_EQ(DecodeText(withTerminator), text);

    HistoryItem item;
    item.contents.push_back({ std::wstring{ ContentType::Text }, withTerminator });
    CHECK_EQ(item.Text(), std::optional<std::wstring>{ text });
    CHECK(!item.Html().has_value());
    CHECK(!item.Rtf().has_value());
    CHECK(item.Files().empty());
    CHECK(item.Image() == nullptr);
}

TEST(HistoryItem_FindAndHas)
{
    HistoryItem item = TextItem(L"x");
    item.contents.push_back({ std::wstring{ ContentType::FromKleio }, {} });
    CHECK(item.Has(ContentType::Text));
    CHECK(item.FromKleio());
    CHECK(!item.Has(ContentType::Html));
    CHECK(item.Find(ContentType::Text) == &item.contents[0]);
}

// ---- Supersedes (Maccy: transient types are ignored) ----

TEST(HistoryItem_Supersedes)
{
    HistoryItem rich = TextItem(L"one");
    rich.contents.push_back({ std::wstring{ ContentType::Rtf }, Bytes("two") });
    HistoryItem plain = TextItem(L"one");
    CHECK(rich.Supersedes(plain));
    CHECK(!plain.Supersedes(rich));
    CHECK(plain.Supersedes(plain));

    HistoryItem fromKleio = TextItem(L"one");
    fromKleio.contents.push_back({ std::wstring{ ContentType::FromKleio }, {} });
    fromKleio.contents.push_back({ std::wstring{ ContentType::CanIncludeInClipboardHistory }, { 0, 0, 0, 0 } });
    fromKleio.contents.push_back({ L"Kleio.Whatever", Bytes("x") });
    CHECK(plain.Supersedes(fromKleio));
    CHECK(rich.Supersedes(fromKleio));
    CHECK(fromKleio.Supersedes(plain));

    HistoryItem other = TextItem(L"two");
    CHECK(!rich.Supersedes(other));

    HistoryItem sameTypeDifferentBytes;
    sameTypeDifferentBytes.contents.push_back({ std::wstring{ ContentType::Rtf }, Bytes("three") });
    CHECK(!rich.Supersedes(sameTypeDifferentBytes));
}

// ---- HexColor (SwiftHEXColors semantics as used by ColorImage.from) ----

TEST(HistoryItem_HexColor)
{
    auto color = [](std::wstring_view title) {
        HistoryItem item;
        item.title = title;
        return item.HexColor();
    };
    CHECK_EQ(color(L"fff"), (std::optional<std::uint32_t>{ 0xFFFFFFFFu }));
    CHECK_EQ(color(L"#ff8942"), (std::optional<std::uint32_t>{ 0xFFFF8942u }));
    CHECK_EQ(color(L"#F80"), (std::optional<std::uint32_t>{ 0xFFFF8800u }));
    CHECK_EQ(color(L"#f808"), (std::optional<std::uint32_t>{ 0x88FF8800u }));
    CHECK_EQ(color(L"#ff880080"), (std::optional<std::uint32_t>{ 0x80FF8800u }));
    CHECK_EQ(color(L"ABCDEF"), (std::optional<std::uint32_t>{ 0xFFABCDEFu }));
    CHECK(!color(L"foo").has_value());
    CHECK(!color(L"#12345").has_value());
    CHECK(!color(L"#gggggg").has_value());
    CHECK(!color(L"#").has_value());
    CHECK(!color(L"").has_value());
    CHECK(!color(L" #fff").has_value());
    CHECK(!color(L"##fff").has_value());
    CHECK(!color(L"0xfff").has_value());
}

// ---- RTF extraction ----

TEST(Rtf_ParagraphsTabsAndEscapes)
{
    CHECK_EQ(PlainTextFromRtf("{\\rtf1\\ansi Hello\\par World}"), std::wstring{ L"Hello\nWorld" });
    CHECK_EQ(PlainTextFromRtf("{\\rtf1 a\\tab b\\line c}"), std::wstring{ L"a\tb\nc" });
    CHECK_EQ(PlainTextFromRtf("{\\rtf1 foo\\\nbar}"), std::wstring{ L"foo\nbar" });
    CHECK_EQ(PlainTextFromRtf("{\\rtf1 \\{braces\\} and \\\\ backslash}"), std::wstring{ L"{braces} and \\ backslash" });
    CHECK_EQ(PlainTextFromRtf("{\\rtf1 line\r\nbreaks\nignored}"), std::wstring{ L"linebreaksignored" });
}

TEST(Rtf_HexAndUnicodeEscapes)
{
    CHECK_EQ(PlainTextFromRtf("{\\rtf1\\ansi\\ansicpg1252 caf\\'e9 \\'80}"), std::wstring{ L"caf\u00E9 \u20AC" });
    CHECK_EQ(PlainTextFromRtf("{\\rtf1 \\u8364?5}"), std::wstring{ L"\u20AC" L"5" });
    CHECK_EQ(PlainTextFromRtf("{\\rtf1\\uc2 \\u20320\\'c4\\'e3!}"), std::wstring{ L"\u4F60!" });
    CHECK_EQ(PlainTextFromRtf("{\\rtf1\\uc0 \\u20320\\u22909 ok}"), std::wstring{ L"\u4F60\u597Dok" });
    CHECK_EQ(PlainTextFromRtf("{\\rtf1 \\u-3913?}"), std::wstring(1, static_cast<wchar_t>(65536 - 3913)));
    CHECK_EQ(PlainTextFromRtf("{\\rtf1 \\emdash\\bullet\\lquote x\\rquote\\~}"), std::wstring{ L"\u2014\u2022\u2018x\u2019\u00A0" });
}

TEST(Rtf_SkipsDestinations)
{
    const std::string rtf =
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fnil Calibri;}{\\f1 Times New Roman;}}"
        "{\\colortbl ;\\red0\\green0\\blue0;}"
        "{\\stylesheet{\\s0 Normal;}}"
        "{\\info{\\author Someone}{\\title Doc}}"
        "{\\*\\generator Riched20 10.0.22621}\\viewkind4\\uc1 "
        "\\pard\\sa200\\sl276\\slmult1\\f0\\fs22\\lang9 Visible {\\b bold} text"
        "{\\pict\\wmetafile8 0123456789abcdef}"
        "{\\field{\\*\\fldinst HYPERLINK \"https://example.com\"}{\\fldrslt link}}\\par\n}";
    CHECK_EQ(PlainTextFromRtf(rtf), std::wstring{ L"Visible bold textlink\n" });
}

// ---- CF_HTML extraction ----

TEST(Html_UsesFragmentOffsets)
{
    const std::string cfHtml = MakeCfHtml("<b>Hello</b> <i>world</i>");
    CHECK_EQ(PlainTextFromHtml(cfHtml), std::wstring{ L"Hello world" });

    // Offsets win over markers: point them at a different region.
    std::string payload = "Version:0.9\r\nStartHTML:-1\r\nEndHTML:-1\r\nStartFragment:000000099\r\nEndFragment:000000102\r\n";
    while (payload.size() < 99)
    {
        payload += ' ';
    }
    payload += "abc<!--StartFragment-->xyz<!--EndFragment-->";
    CHECK_EQ(PlainTextFromHtml(payload), std::wstring{ L"abc" });
}

TEST(Html_UsesMarkersWithoutOffsets)
{
    CHECK_EQ(PlainTextFromHtml("Version:0.9\r\n<html><body>before<!--StartFragment--><p>inside</p><!--EndFragment-->after</body></html>"),
             std::wstring{ L"inside" });
}

TEST(Html_PlainHtmlWithoutHeader)
{
    CHECK_EQ(PlainTextFromHtml("<a href='#'>foo</a>"), std::wstring{ L"foo" });
    CHECK_EQ(PlainTextFromHtml("<a title=\"x > y\" href='#'>foo</a> & bar"), std::wstring{ L"foo & bar" });
}

TEST(Html_EntitiesAndWhitespace)
{
    CHECK_EQ(PlainTextFromHtml("&amp;&lt;&gt;&quot;&#39;&#x41;&#66;&nbsp;x &hellip; &unknown; &copy;"),
             std::wstring{ L"&<>\"'AB x \u2026 &unknown; \u00A9" });
    CHECK_EQ(PlainTextFromHtml("a   \r\n  b\t\tc"), std::wstring{ L"a b c" });
    CHECK_EQ(PlainTextFromHtml("<pre>a  b\r\n  c</pre>"), std::wstring{ L"a  b\n  c" });
}

TEST(Html_BlockElementsBecomeNewlines)
{
    CHECK_EQ(PlainTextFromHtml("<p>one</p><p>two</p>"), std::wstring{ L"one\ntwo" });
    CHECK_EQ(PlainTextFromHtml("line1<br>line2<br/>line3<BR />"), std::wstring{ L"line1\nline2\nline3" });
    CHECK_EQ(PlainTextFromHtml("<div>a</div><div>b</div>"), std::wstring{ L"a\nb" });
    CHECK_EQ(PlainTextFromHtml("<ul><li>x</li><li>y</li></ul>"), std::wstring{ L"x\ny" });
    CHECK_EQ(PlainTextFromHtml("<table><tr><td>a</td><td>b</td></tr><tr><td>c</td></tr></table>"), std::wstring{ L"a\tb\nc" });
}

TEST(Html_SkipsScriptsStylesAndComments)
{
    CHECK_EQ(PlainTextFromHtml("<style>p { color: red; }</style><script>var a = '<b>';</script><!-- note -->Text<title>T</title>"),
             std::wstring{ L"Text" });
}

TEST(Html_DecodesUtf8)
{
    CHECK_EQ(PlainTextFromHtml(MakeCfHtml("caf\xC3\xA9 \xE4\xB8\x96\xE7\x95\x8C")), std::wstring{ L"caf\u00E9 \u4E16\u754C" });
}
