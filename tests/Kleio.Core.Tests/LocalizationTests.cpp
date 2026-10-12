#include "pch.h"
#include "TestFramework.h"

#include "Core/Localization.h"
#include "Core/Scripting.h"

using namespace Kleio;

TEST(Localization_FormatReplacesNumberedPlaceholders)
{
    CHECK_EQ(Localization::Format(L"Kleio {0} is now available. You have {1}.", { L"1.2", L"1.1" }),
             std::wstring{ L"Kleio 1.2 is now available. You have 1.1." });
    // Translations may reorder and repeat placeholders.
    CHECK_EQ(Localization::Format(L"{1} \u2190 {0} ({1})", { L"a", L"b" }), std::wstring{ L"b \u2190 a (b)" });
    CHECK_EQ(Localization::Format(L"no placeholders", { L"x" }), std::wstring{ L"no placeholders" });
}

TEST(Localization_FormatKeepsBracesAndMissingArguments)
{
    CHECK_EQ(Localization::Format(L"{{0}} {0}", { L"x" }), std::wstring{ L"{0} x" });
    CHECK_EQ(Localization::Format(L"{0} {1}", { L"x" }), std::wstring{ L"x {1}" });
    CHECK_EQ(Localization::Format(L"^\\d{6}$ {0}", { L"x" }), std::wstring{ L"^\\d{6}$ x" });
    CHECK_EQ(Localization::Format(L"{", {}), std::wstring{ L"{" });
}

namespace
{
    std::wstring GermanLookup(std::wstring_view key)
    {
        if (key == L"Script_UnknownOption")
        {
            return L"Unbekannte Option: {0}";
        }
        return {};
    }
}

TEST(Localization_TextUsesTheInstalledLookupOrTheEnglishFallback)
{
    CHECK_EQ(Localization::Text(L"Script_UnknownOption", L"Unknown option: {0}"), std::wstring{ L"Unknown option: {0}" });
    Localization::SetLookup(&GermanLookup);
    CHECK_EQ(Scripting::Parse({ L"--get", L"--bogus" }).error, std::wstring{ L"Unbekannte Option: --bogus" });
    // Keys the lookup doesn't know keep their English text.
    CHECK_EQ(Scripting::Parse({ L"--get", L"0" }).error, std::wstring{ L"The item number must be 1 or greater." });
    Localization::SetLookup(nullptr);
    CHECK_EQ(Scripting::Parse({ L"--get", L"--bogus" }).error, std::wstring{ L"Unknown option: --bogus" });
}
