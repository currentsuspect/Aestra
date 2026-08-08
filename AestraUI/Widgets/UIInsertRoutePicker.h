// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "UIMixerRoutePicker.h"

namespace AestraUI {

/**
 * Compatibility wrapper for code written before mixer destinations were named consistently.
 * New code must use UIMixerRoutePicker.
 */
class UIInsertRoutePicker final : public UIMixerRoutePicker {
public:
    using UIMixerRoutePicker::UIMixerRoutePicker;
};

} // namespace AestraUI
