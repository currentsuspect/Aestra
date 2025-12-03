/**
 * @file component.hpp
 * @brief UI Component base - Migration wrapper
 * 
 * Wraps your existing NUIComponent system.
 */

#pragma once

// Include YOUR existing UI component system
#include "NomadUI/Core/NUIComponent.h"
#include "NomadUI/Core/NUITypes.h"

namespace nomad::ui {

//=============================================================================
// Re-export existing types in new namespace
//=============================================================================

using Component = ::NUIComponent;
using Rect = ::NUIRect;
using Point = ::NUIPoint;
using Size = ::NUISize;
using Color = ::NUIColor;

// Component state types if they exist
// using ComponentState = ::NUIComponentState;

} // namespace nomad::ui
