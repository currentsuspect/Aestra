/**
 * @file widgets.hpp
 * @brief UI Widgets - Migration wrapper
 * 
 * Wraps your existing NUI widget library.
 */

#pragma once

// Include YOUR existing widget implementations
#include "NomadUI/Core/NUIButton.h"
#include "NomadUI/Core/NUISlider.h"
#include "NomadUI/Core/NUILabel.h"
#include "NomadUI/Core/NUICheckbox.h"
#include "NomadUI/Core/NUIDropdown.h"
#include "NomadUI/Core/NUITextInput.h"
#include "NomadUI/Core/NUIProgressBar.h"
#include "NomadUI/Core/NUIScrollbar.h"
#include "NomadUI/Core/NUIContextMenu.h"

namespace nomad::ui {

//=============================================================================
// Re-export existing widget types in new namespace
//=============================================================================

// Core widgets
using Button = ::NUIButton;
using Slider = ::NUISlider;
using Label = ::NUILabel;
using Checkbox = ::NUICheckbox;
using Dropdown = ::NUIDropdown;
using TextInput = ::NUITextInput;
using ProgressBar = ::NUIProgressBar;
using Scrollbar = ::NUIScrollbar;
using ContextMenu = ::NUIContextMenu;

} // namespace nomad::ui
