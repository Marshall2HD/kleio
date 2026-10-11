#pragma once

#include "PopupSplitter.g.h"

namespace winrt::Kleio::implementation
{
    struct PopupSplitter : PopupSplitterT<PopupSplitter>
    {
        PopupSplitter() = default;

        void InitializeComponent();
    };
}

namespace winrt::Kleio::factory_implementation
{
    struct PopupSplitter : PopupSplitterT<PopupSplitter, implementation::PopupSplitter>
    {
    };
}
