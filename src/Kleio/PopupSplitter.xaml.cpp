#include "pch.h"
#include "PopupSplitter.xaml.h"
#if __has_include("PopupSplitter.g.cpp")
#include "PopupSplitter.g.cpp"
#endif

namespace winrt::Kleio::implementation
{
    void PopupSplitter::InitializeComponent()
    {
        PopupSplitterT<PopupSplitter>::InitializeComponent();

        try
        {
            // UIElement.ProtectedCursor is only settable from a derived class.
            this->ProtectedCursor(winrt::Microsoft::UI::Input::InputSystemCursor::Create(
                winrt::Microsoft::UI::Input::InputSystemCursorShape::SizeWestEast));
        }
        catch (...)
        {
        }
    }
}
