#include "pch.h"
#include "TestFramework.h"
#include "TestSupport.h"

#include "Core/Scripting.h"

#include <cstdio>
#include <cstring>

using namespace Kleio;
using namespace Kleio::Scripting;

namespace
{
    ParseResult P(std::vector<std::wstring> args)
    {
        return Parse(args);
    }

    HistoryItemPtr TextItem(std::wstring_view text)
    {
        auto item = std::make_shared<HistoryItem>();
        item->contents.push_back({ std::wstring{ ContentType::Text }, EncodeText(text) });
        return item;
    }

    std::string AsString(std::vector<std::uint8_t> const& bytes)
    {
        return std::string{ bytes.begin(), bytes.end() };
    }
}

TEST(Scripting_NoVerbIsNotAScriptingCommand)
{
    auto const none = P({});
    CHECK(!none.command);
    CHECK(none.error.empty());
    auto const pause = P({ L"--pause" });
    CHECK(!pause.command);
    CHECK(pause.error.empty());
}

TEST(Scripting_GetDefaultsToSelectedItem)
{
    auto const result = P({ L"--get" });
    CHECK(result.command.has_value());
    CHECK_EQ(result.command->verb, Verb::Get);
    CHECK(!result.command->number.has_value());
    CHECK_EQ(result.command->format, Format::Auto);
}

TEST(Scripting_GetWithNumberFormatAndOutput)
{
    auto const result = P({ L"--get", L"3", L"--format", L"image", L"--output", L"C:\\temp\\a.png" });
    CHECK(result.command.has_value());
    CHECK_EQ(result.command->number, std::optional<std::int32_t>{ 3 });
    CHECK_EQ(result.command->format, Format::Image);
    CHECK_EQ(result.command->output, std::wstring{ L"C:\\temp\\a.png" });

    auto const inlineValue = P({ L"--get", L"--format=HTML", L"2" });
    CHECK(inlineValue.command.has_value());
    CHECK_EQ(inlineValue.command->format, Format::Html);
    CHECK_EQ(inlineValue.command->number, std::optional<std::int32_t>{ 2 });
}

TEST(Scripting_SelectAndDeleteDefaultToFirstItem)
{
    CHECK_EQ(P({ L"--select" }).command->number, std::optional<std::int32_t>{ 1 });
    CHECK_EQ(P({ L"--delete" }).command->number, std::optional<std::int32_t>{ 1 });
    CHECK_EQ(P({ L"/select", L"4" }).command->number, std::optional<std::int32_t>{ 4 });
}

TEST(Scripting_ClearAcceptsYes)
{
    auto const result = P({ L"--clear", L"--yes" });
    CHECK(result.command.has_value());
    CHECK_EQ(result.command->verb, Verb::Clear);
    CHECK(result.command->yes);
}

TEST(Scripting_InvalidArgumentsAreReported)
{
    CHECK(!P({ L"--get", L"0" }).error.empty());
    CHECK(!P({ L"--get", L"-1" }).error.empty());
    CHECK(!P({ L"--get", L"--format", L"pdf" }).error.empty());
    CHECK(!P({ L"--select", L"--format", L"text" }).error.empty());
    CHECK(!P({ L"--get", L"--select" }).error.empty());
    CHECK(!P({ L"--clear", L"2" }).error.empty());
    CHECK(!P({ L"--delete", L"1", L"2" }).error.empty());
    CHECK(!P({ L"--get", L"--bogus" }).error.empty());
}

TEST(Scripting_Help)
{
    CHECK_EQ(P({ L"--help" }).command->verb, Verb::Help);
    CHECK_EQ(P({ L"/?" }).command->verb, Verb::Help);
    CHECK(!Usage().empty());
}

TEST(Scripting_ItemAtIsOneBasedAndBoundsChecked)
{
    std::vector<HistoryItemPtr> items{ TextItem(L"a"), TextItem(L"b") };
    CHECK(ItemAt(items, 1) == items[0]);
    CHECK(ItemAt(items, 2) == items[1]);
    CHECK(ItemAt(items, 0) == nullptr);
    CHECK(ItemAt(items, 3) == nullptr);
    CHECK(ItemAt(items, -1) == nullptr);
}

TEST(Scripting_ExtractTextAsUtf8)
{
    auto item = TextItem(L"h\u00e9llo \u20ac");
    auto payload = Extract(*item, Format::Auto);
    CHECK(payload.has_value());
    CHECK_EQ(payload->format, Format::Text);
    CHECK_EQ(AsString(payload->bytes), std::string{ "h\xC3\xA9llo \xE2\x82\xAC" });
    CHECK(!Extract(*item, Format::Image).has_value());
    CHECK(!Extract(*item, Format::Files).has_value());
}

TEST(Scripting_ExtractFilesOnePerLine)
{
    HistoryItem item;
    item.contents.push_back({ std::wstring{ ContentType::Files }, EncodeFiles({ L"C:\\a.txt", L"C:\\b.txt" }) });
    auto payload = Extract(item, Format::Auto);
    CHECK(payload.has_value());
    CHECK_EQ(payload->format, Format::Files);
    CHECK_EQ(AsString(payload->bytes), std::string{ "C:\\a.txt\r\nC:\\b.txt" });
}

TEST(Scripting_HtmlFromCfHtmlDropsHeader)
{
    std::string const html = "<html><body><b>x</b></body></html>";
    char header[128]{};
    std::snprintf(header, sizeof(header), "Version:0.9\r\nStartHTML:%09d\r\nEndHTML:%09d\r\n", 0, 0);
    std::size_t const start = std::strlen(header);
    std::snprintf(header, sizeof(header), "Version:0.9\r\nStartHTML:%09zu\r\nEndHTML:%09zu\r\n", start, start + html.size());
    CHECK_EQ(HtmlFromCfHtml(std::string{ header } + html), html);
    CHECK_EQ(HtmlFromCfHtml("<p>plain</p>"), std::string{ "<p>plain</p>" });
}
