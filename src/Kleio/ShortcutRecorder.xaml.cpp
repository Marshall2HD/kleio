#include "pch.h"
#include "ShortcutRecorder.xaml.h"
#if __has_include("ShortcutRecorder.g.cpp")
#include "ShortcutRecorder.g.cpp"
#endif

#include <winrt/Windows.UI.Core.h>

#include "AppController.h"
#include "Core/Settings.h"
#include "Platform/KeyboardHook.h"
#include "SettingsHelpers.h"
#include "Strings.h"

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Input;
    using namespace Microsoft::UI::Xaml::Automation;
}

namespace
{
    bool IsModifierKey(std::uint32_t vk) noexcept
    {
        switch (vk)
        {
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_LWIN:
        case VK_RWIN:
            return true;
        default:
            return false;
        }
    }

    bool IsFunctionKey(std::uint32_t vk) noexcept
    {
        return vk >= VK_F1 && vk <= VK_F24;
    }

    // Shift alone is not a usable modifier for a global/item shortcut (it would swallow typing).
    bool HasRequiredModifier(Kleio::Modifiers modifiers) noexcept
    {
        return Kleio::HasFlag(modifiers, Kleio::Modifiers::Ctrl) || Kleio::HasFlag(modifiers, Kleio::Modifiers::Alt) ||
               Kleio::HasFlag(modifiers, Kleio::Modifiers::Win);
    }

    std::vector<std::wstring> ModifierNames(Kleio::Modifiers modifiers)
    {
        std::vector<std::wstring> names;
        for (auto const flag : { Kleio::Modifiers::Ctrl, Kleio::Modifiers::Alt, Kleio::Modifiers::Shift, Kleio::Modifiers::Win })
        {
            if (Kleio::HasFlag(modifiers, flag))
            {
                names.push_back(Kleio::Strings::ModifierName(flag));
            }
        }
        return names;
    }
}

namespace winrt::Kleio::implementation
{
    ShortcutRecorder::~ShortcutRecorder()
    {
        ResumeGlobalHotKeys();
    }

    void ShortcutRecorder::InitializeComponent()
    {
        ShortcutRecorderT<ShortcutRecorder>::InitializeComponent();
        UpdateView();
    }

    void ShortcutRecorder::Shortcut(winrt::hstring const& value)
    {
        if (m_shortcut == value)
        {
            return;
        }
        m_shortcut = value;
        UpdateView();
    }

    winrt::event_token ShortcutRecorder::ShortcutChanged(
        winrt::Windows::Foundation::TypedEventHandler<winrt::Kleio::ShortcutRecorder, winrt::Windows::Foundation::IInspectable> const& handler)
    {
        return m_shortcutChanged.add(handler);
    }

    void ShortcutRecorder::ShortcutChanged(winrt::event_token const& token) noexcept
    {
        m_shortcutChanged.remove(token);
    }

    void ShortcutRecorder::EditButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            if (m_recording)
            {
                StopRecording();
            }
            else
            {
                StartRecording();
            }
        }
        catch (...)
        {
        }
    }

    void ShortcutRecorder::EditButton_LostFocus(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            if (m_recording)
            {
                StopRecording();
            }
        }
        catch (...)
        {
        }
    }

    void ShortcutRecorder::ClearButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            Commit(L"");
            EditButton().Focus(winrt::FocusState::Programmatic);
        }
        catch (...)
        {
        }
    }

    void ShortcutRecorder::Recorder_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            if (m_recording)
            {
                StopRecording();
            }
        }
        catch (...)
        {
        }
        ResumeGlobalHotKeys();
    }

    void ShortcutRecorder::Root_PreviewKeyDown(winrt::Windows::Foundation::IInspectable const&, winrt::KeyRoutedEventArgs const& args)
    {
        if (!m_recording)
        {
            return;
        }
        try
        {
            auto const vk = static_cast<std::uint32_t>(args.Key());
            auto const modifiers = ::Kleio::CurrentModifiers();

            // Plain Tab leaves the control (keyboard navigation keeps working).
            if (vk == VK_TAB && modifiers == ::Kleio::Modifiers::None)
            {
                StopRecording();
                return;
            }
            args.Handled(true);
            if (args.KeyStatus().WasKeyDown)
            {
                return; // auto-repeat
            }
            if (vk == 0 || vk == VK_PACKET || vk == VK_PROCESSKEY || IsModifierKey(vk))
            {
                m_pendingModifiers = modifiers;
                m_hint.clear();
                UpdateView();
                return;
            }
            if (modifiers == ::Kleio::Modifiers::None)
            {
                if (vk == VK_ESCAPE)
                {
                    StopRecording();
                    return;
                }
                if (vk == VK_BACK || vk == VK_DELETE)
                {
                    Commit(L"");
                    return;
                }
            }
            if (m_requireModifier && !HasRequiredModifier(modifiers) && !IsFunctionKey(vk))
            {
                m_pendingModifiers = modifiers;
                m_hint = ::Kleio::Strings::Format(L"Settings_IncludeModifier", ::Kleio::Strings::ModifierName(::Kleio::Modifiers::Ctrl),
                                                  ::Kleio::Strings::ModifierName(::Kleio::Modifiers::Alt),
                                                  ::Kleio::Strings::ModifierName(::Kleio::Modifiers::Win));
                UpdateView();
                return;
            }
            ::Kleio::KeyShortcut shortcut;
            shortcut.modifiers = modifiers;
            shortcut.key = vk;
            Commit(shortcut.ToString());
        }
        catch (...)
        {
        }
    }

    void ShortcutRecorder::Root_PreviewKeyUp(winrt::Windows::Foundation::IInspectable const&, winrt::KeyRoutedEventArgs const& args)
    {
        if (!m_recording)
        {
            return;
        }
        try
        {
            args.Handled(true);
            auto const modifiers = ::Kleio::CurrentModifiers();
            if (modifiers != m_pendingModifiers)
            {
                m_pendingModifiers = modifiers;
                UpdateView();
            }
        }
        catch (...)
        {
        }
    }

    void ShortcutRecorder::StartRecording()
    {
        m_recording = true;
        m_pendingModifiers = ::Kleio::Modifiers::None;
        m_hint.clear();
        SuspendGlobalHotKeys();
        EditButton().Focus(winrt::FocusState::Programmatic);
        UpdateView();
    }

    void ShortcutRecorder::StopRecording()
    {
        m_recording = false;
        m_pendingModifiers = ::Kleio::Modifiers::None;
        m_hint.clear();
        ResumeGlobalHotKeys();
        UpdateView();
    }

    void ShortcutRecorder::Commit(std::wstring const& shortcut)
    {
        // Restore the global hot keys first; the page then applies the new value (and calls
        // AppController::ApplyHotKeys again for the popup shortcut).
        StopRecording();
        winrt::hstring const value{ shortcut };
        if (value == m_shortcut)
        {
            return;
        }
        m_shortcut = value;
        UpdateView();
        m_shortcutChanged(*this, nullptr);
    }

    void ShortcutRecorder::AddKeycap(std::wstring const& text, bool pending)
    {
        winrt::Border border;
        winrt::TextBlock block;
        block.Text(text);
        auto borderStyle = ::Kleio::SettingsUi::FindResource(pending ? L"SettingsKeycapPendingBorderStyle" : L"SettingsKeycapBorderStyle");
        auto textStyle = ::Kleio::SettingsUi::FindResource(pending ? L"SettingsKeycapPendingTextStyle" : L"SettingsKeycapTextStyle");
        if (borderStyle && textStyle)
        {
            border.Style(borderStyle.as<winrt::Style>());
            block.Style(textStyle.as<winrt::Style>());
        }
        else
        {
            border.Padding(winrt::Thickness{ 8, 3, 8, 4 });
            border.MinWidth(30);
            border.BorderThickness(winrt::Thickness{ 1, 1, 1, 1 });
            border.CornerRadius(winrt::CornerRadius{ 4, 4, 4, 4 });
            block.HorizontalAlignment(winrt::HorizontalAlignment::Center);
        }
        border.Child(block);
        KeysPanel().Children().Append(border);
    }

    void ShortcutRecorder::UpdateView()
    {
        try
        {
            KeysPanel().Children().Clear();
            std::wstring status;
            std::wstring spoken;

            if (m_recording)
            {
                for (auto const& name : ModifierNames(m_pendingModifiers))
                {
                    AddKeycap(name, true);
                }
                if (!m_hint.empty())
                {
                    status = m_hint;
                }
                else if (m_pendingModifiers == ::Kleio::Modifiers::None)
                {
                    status = ::Kleio::Strings::Get(L"Settings_PressShortcut");
                }
                spoken = ::Kleio::Strings::Get(L"Settings_Recording");
            }
            else
            {
                auto const parsed = ::Kleio::KeyShortcut::Parse(std::wstring_view{ m_shortcut });
                if (parsed && !parsed->IsEmpty())
                {
                    for (auto const& name : ModifierNames(parsed->modifiers))
                    {
                        AddKeycap(name, false);
                    }
                    AddKeycap(::Kleio::Strings::KeyText(parsed->key), false);
                    spoken = ::Kleio::Strings::ShortcutText(*parsed);
                }
                else
                {
                    status = ::Kleio::Strings::Get(L"Settings_NotSet");
                    spoken = status;
                }
            }

            StatusText().Text(status);
            StatusText().Visibility(status.empty() ? winrt::Visibility::Collapsed : winrt::Visibility::Visible);
            KeysPanel().Visibility(KeysPanel().Children().Size() == 0 ? winrt::Visibility::Collapsed : winrt::Visibility::Visible);
            EditGlyph().Glyph(m_recording ? L"" : L""); // Record / Edit
            ClearButton().Visibility(!m_recording && !m_shortcut.empty() ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);

            if (m_recording)
            {
                if (auto accent = ::Kleio::SettingsUi::FindResource(L"AccentFillColorDefaultBrush").try_as<winrt::Microsoft::UI::Xaml::Media::Brush>())
                {
                    EditButton().BorderBrush(accent);
                }
            }
            else
            {
                EditButton().ClearValue(winrt::Control::BorderBrushProperty());
            }

            // Screen readers: "<label set by the page>: Ctrl+Alt+V".
            std::wstring label;
            if (auto self = this->try_as<winrt::DependencyObject>())
            {
                label = winrt::AutomationProperties::GetName(self);
            }
            if (label.empty())
            {
                label = ::Kleio::Strings::Get(L"Settings_Shortcut");
            }
            winrt::AutomationProperties::SetName(EditButton(), winrt::hstring{ ::Kleio::Strings::Format(L"Settings_NameValue", label, spoken) });
            winrt::AutomationProperties::SetName(ClearButton(), winrt::hstring{ ::Kleio::Strings::Format(L"Settings_ClearNamed", label) });
        }
        catch (...)
        {
        }
    }

    void ShortcutRecorder::SuspendGlobalHotKeys()
    {
        if (m_hotKeysSuspended)
        {
            return;
        }
        m_hotKeysSuspended = true;
        // Unregisters the popup hot key and the Win+V hook without touching the saved settings,
        // so the recorder sees every key combination.
        ::Kleio::AppController::Current().SuspendHotKeys(true);
    }

    void ShortcutRecorder::ResumeGlobalHotKeys() noexcept
    {
        if (!m_hotKeysSuspended)
        {
            return;
        }
        m_hotKeysSuspended = false;
        try
        {
            ::Kleio::AppController::Current().SuspendHotKeys(false);
        }
        catch (...)
        {
        }
    }
}
