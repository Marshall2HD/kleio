#include "pch.h"
#include "Platform/AppInfo.h"
#include "Platform/ImageCodec.h"

#include <cmath>
#include <cwchar>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "ole32.lib")

namespace Kleio::AppInfo
{
    namespace
    {
        std::mutex g_cacheMutex;

        std::wstring ProcessPath(DWORD pid)
        {
            if (pid == 0)
            {
                return {};
            }
            wil::unique_handle process{ OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) };
            if (!process)
            {
                return {};
            }
            std::wstring buffer(32768, L'\0');
            DWORD size = static_cast<DWORD>(buffer.size());
            if (!QueryFullProcessImageNameW(process.get(), 0, buffer.data(), &size))
            {
                return {};
            }
            buffer.resize(size);
            return buffer;
        }

        bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept
        {
            return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
        }

        struct HostedAppSearch
        {
            DWORD hostPid = 0;
            DWORD foundPid = 0;
        };

        BOOL CALLBACK FindHostedApp(HWND child, LPARAM param) noexcept
        {
            auto* search = reinterpret_cast<HostedAppSearch*>(param);
            DWORD pid = 0;
            GetWindowThreadProcessId(child, &pid);
            if (pid != 0 && pid != search->hostPid)
            {
                search->foundPid = pid;
                return FALSE;
            }
            return TRUE;
        }
    }

    std::wstring FileName(std::wstring const& path)
    {
        const auto separator = path.find_last_of(L"\\/");
        return separator == std::wstring::npos ? path : path.substr(separator + 1);
    }

    std::wstring ProcessPathForWindow(HWND hwnd)
    {
        try
        {
            if (!hwnd)
            {
                return {};
            }
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            std::wstring path = ProcessPath(pid);

            // UWP apps live inside ApplicationFrameHost.exe; the real app owns a child CoreWindow.
            if (EqualsIgnoreCase(FileName(path), L"ApplicationFrameHost.exe"))
            {
                HostedAppSearch search{ pid, 0 };
                EnumChildWindows(hwnd, FindHostedApp, reinterpret_cast<LPARAM>(&search));
                if (search.foundPid != 0)
                {
                    std::wstring hosted = ProcessPath(search.foundPid);
                    if (!hosted.empty())
                    {
                        return hosted;
                    }
                }
            }
            return path;
        }
        catch (...)
        {
            return {};
        }
    }

    std::wstring ForegroundProcessPath()
    {
        return ProcessPathForWindow(GetForegroundWindow());
    }

    std::wstring DisplayName(std::wstring const& exePath)
    {
        try
        {
            if (exePath.empty())
            {
                return {};
            }

            static std::map<std::wstring, std::wstring> cache;
            {
                std::lock_guard lock{ g_cacheMutex };
                if (auto it = cache.find(exePath); it != cache.end())
                {
                    return it->second;
                }
            }

            std::wstring name;
            DWORD handle = 0;
            const DWORD size = GetFileVersionInfoSizeW(exePath.c_str(), &handle);
            if (size > 0)
            {
                std::vector<std::uint8_t> data(size);
                if (GetFileVersionInfoW(exePath.c_str(), 0, size, data.data()))
                {
                    struct Translation
                    {
                        WORD language;
                        WORD codePage;
                    };
                    std::vector<std::wstring> blocks;
                    Translation* translations = nullptr;
                    UINT translationsSize = 0;
                    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &translationsSize) &&
                        translations && translationsSize >= sizeof(Translation))
                    {
                        wchar_t block[64]{};
                        swprintf_s(block, std::size(block), L"\\StringFileInfo\\%04x%04x\\FileDescription", translations[0].language, translations[0].codePage);
                        blocks.emplace_back(block);
                    }
                    blocks.emplace_back(L"\\StringFileInfo\\040904b0\\FileDescription");
                    blocks.emplace_back(L"\\StringFileInfo\\040904e4\\FileDescription");

                    for (auto const& block : blocks)
                    {
                        wchar_t* value = nullptr;
                        UINT length = 0;
                        if (VerQueryValueW(data.data(), block.c_str(), reinterpret_cast<void**>(&value), &length) && value && length > 1)
                        {
                            name.assign(value, wcsnlen(value, length));
                            // Trim surrounding whitespace.
                            const auto first = name.find_first_not_of(L" \t");
                            const auto last = name.find_last_not_of(L" \t");
                            name = first == std::wstring::npos ? std::wstring{} : name.substr(first, last - first + 1);
                            if (!name.empty())
                            {
                                break;
                            }
                        }
                    }
                }
            }

            if (name.empty())
            {
                name = FileName(exePath);
                if (name.size() > 4 && EqualsIgnoreCase(std::wstring_view{ name }.substr(name.size() - 4), L".exe"))
                {
                    name.resize(name.size() - 4);
                }
            }

            std::lock_guard lock{ g_cacheMutex };
            cache[exePath] = name;
            return name;
        }
        catch (...)
        {
            return FileName(exePath);
        }
    }

    std::vector<std::uint8_t> IconPng(std::wstring const& exePath, double scale)
    {
        try
        {
            if (exePath.empty())
            {
                return {};
            }
            if (!(scale > 0.0) || scale > 16.0)
            {
                scale = 1.0;
            }
            const int size = (std::max)(16, static_cast<int>(std::lround(32.0 * scale)));

            static std::map<std::wstring, std::vector<std::uint8_t>> cache;
            const std::wstring key = exePath + L"|" + std::to_wstring(size);
            {
                std::lock_guard lock{ g_cacheMutex };
                if (auto it = cache.find(key); it != cache.end())
                {
                    return it->second;
                }
            }

            std::vector<std::uint8_t> png;

            // Preferred: the shell's icon at the exact size (handles packaged apps too).
            winrt::com_ptr<IShellItemImageFactory> imageFactory;
            if (SUCCEEDED(SHCreateItemFromParsingName(exePath.c_str(), nullptr, IID_PPV_ARGS(imageFactory.put()))) && imageFactory)
            {
                HBITMAP rawBitmap = nullptr;
                if (SUCCEEDED(imageFactory->GetImage(SIZE{ size, size }, SIIGBF_ICONONLY, &rawBitmap)) && rawBitmap)
                {
                    wil::unique_hbitmap bitmap{ rawBitmap };
                    png = ImageCodec::BitmapToPng(bitmap.get());
                }
            }

            // Fallback: SHGetFileInfo large icon.
            if (png.empty())
            {
                SHFILEINFOW info{};
                if (SHGetFileInfoW(exePath.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON) && info.hIcon)
                {
                    wil::unique_hicon icon{ info.hIcon };
                    png = ImageCodec::IconToPng(icon.get());
                }
            }

            std::lock_guard lock{ g_cacheMutex };
            cache[key] = png;
            return png;
        }
        catch (...)
        {
            return {};
        }
    }
}
