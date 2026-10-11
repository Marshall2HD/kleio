#include "pch.h"
#include "TestFramework.h"
#include "TestSupport.h"

#include "Core/Storage.h"

#include <cstdio>
#include <exception>
#include <random>

namespace KleioTests
{
    namespace
    {
        int g_currentFailures = 0;
        int g_totalFailures = 0;
    }

    std::vector<TestCase>& Registry()
    {
        static std::vector<TestCase> registry;
        return registry;
    }

    void ReportFailure(char const* file, int line, std::string const& message)
    {
        ++g_currentFailures;
        ++g_totalFailures;
        std::printf("    %s(%d): %s\n", file, line, message.c_str());
    }

    std::string Narrow(std::wstring_view text)
    {
        std::string result;
        for (wchar_t c : text)
        {
            const auto code = static_cast<std::uint32_t>(c);
            if (code >= 0x20 && code < 0x7F)
            {
                result += static_cast<char>(code);
            }
            else
            {
                char buffer[16] = {};
                std::snprintf(buffer, sizeof(buffer), "\\u%04X", static_cast<unsigned>(code));
                result += buffer;
            }
        }
        return result;
    }

    // ---- TestSupport ----

    std::optional<std::wstring> MemoryStore::Read(std::wstring_view key) const
    {
        ++reads;
        auto it = values.find(key);
        if (it == values.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    void MemoryStore::Write(std::wstring_view key, std::wstring_view value)
    {
        ++writes;
        values.insert_or_assign(std::wstring{ key }, std::wstring{ value });
    }

    void MemoryStore::Remove(std::wstring_view key)
    {
        auto it = values.find(key);
        if (it != values.end())
        {
            values.erase(it);
        }
    }

    MemoryStore& ResetSettings()
    {
        return ResetSettings({});
    }

    MemoryStore& ResetSettings(std::map<std::wstring, std::wstring, std::less<>> values)
    {
        auto store = std::make_unique<MemoryStore>();
        store->values = std::move(values);
        MemoryStore& reference = *store;
        Kleio::Settings::UseStore(std::move(store));
        return reference;
    }

    TempDatabase::TempDatabase()
    {
        static std::mt19937_64 engine{ std::random_device{}() };
        std::error_code ec;
        auto directory = std::filesystem::temp_directory_path(ec);
        if (ec)
        {
            directory = std::filesystem::current_path();
        }
        m_path = directory / ("kleio-core-tests-" + std::to_string(engine()) + ".sqlite");
    }

    TempDatabase::~TempDatabase()
    {
        static constexpr char const* kSuffixes[] = { "", "-wal", "-shm", "-journal" };
        for (auto const* suffix : kSuffixes)
        {
            std::filesystem::path file = m_path;
            file += suffix;
            std::error_code ec;
            std::filesystem::remove(file, ec);
        }
    }

    Kleio::Timestamp NextTimestamp()
    {
        static Kleio::Timestamp next = 1'700'000'000'000;
        return next += 1000;
    }

    Kleio::HistoryItemPtr MakeTextItem(std::wstring_view text)
    {
        auto item = std::make_shared<Kleio::HistoryItem>();
        item->contents.push_back({ std::wstring{ Kleio::ContentType::Text }, Kleio::EncodeText(text) });
        item->firstCopiedAt = item->lastCopiedAt = NextTimestamp();
        item->title = item->GenerateTitle(true);
        return item;
    }

    std::vector<std::uint8_t> Bytes(std::string_view text)
    {
        return std::vector<std::uint8_t>(text.begin(), text.end());
    }
}

// The app defines these in Core/AppData.cpp (WinRT); the tests provide WinRT-free versions.
namespace Kleio
{
    std::unique_ptr<SettingsStore> CreateDefaultSettingsStore()
    {
        return std::make_unique<KleioTests::MemoryStore>();
    }

    std::filesystem::path Storage::DefaultPath()
    {
        std::error_code ec;
        return std::filesystem::temp_directory_path(ec) / "kleio-core-tests-default.sqlite";
    }
}

int main()
{
    using namespace KleioTests;
    int failedTests = 0;
    for (auto const& test : Registry())
    {
        g_currentFailures = 0;
        try
        {
            test.function();
        }
        catch (std::exception const& e)
        {
            ReportFailure(test.name, 0, std::string{ "unhandled exception: " } + e.what());
        }
        catch (...)
        {
            ReportFailure(test.name, 0, "unhandled non-standard exception");
        }
        if (g_currentFailures > 0)
        {
            ++failedTests;
            std::printf("[FAIL] %s\n", test.name);
        }
        else
        {
            std::printf("[ OK ] %s\n", test.name);
        }
    }
    std::printf("\n%zu tests, %d failed, %d failed checks\n", Registry().size(), failedTests, g_totalFailures);
    std::fflush(stdout);
    return g_totalFailures;
}
