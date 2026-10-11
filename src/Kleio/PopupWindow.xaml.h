#pragma once

#include "PopupWindow.g.h"

namespace winrt::Kleio::implementation
{
    struct PopupWindow : PopupWindowT<PopupWindow>
    {
        PopupWindow() = default;
    };
}

namespace winrt::Kleio::factory_implementation
{
    struct PopupWindow : PopupWindowT<PopupWindow, implementation::PopupWindow>
    {
    };
}
