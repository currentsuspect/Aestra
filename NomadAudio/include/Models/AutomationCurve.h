// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include "TimeTypes.h"
#include "NomadUUID.h"

namespace Nomad {
namespace Audio {

/**
 * @brief A single node in an automation envelope.
 */
struct AutomationPoint {
    double beat = 0.0;
    double value = 0.0;
    float curve = 0.0f; // 0 = linear, -1..1 for bezier/power curves (Prep for v3.2)
    
    // UI selection state (not persisted)
    bool selected = false;

    bool operator<(const AutomationPoint& other) const {
        return beat < other.beat;
    }
};

/**
 * @brief Automation targets (parameters that can be automated)
 */
enum class AutomationTarget {
    None,
    Volume,
    Pan,
    Mute,
    PluginParam
};

/**
 * @brief A collection of automation points targeting a specific parameter.
 */
class AutomationCurve {
public:
    AutomationCurve() = default;
    AutomationCurve(const std::string& paramName, AutomationTarget target = AutomationTarget::None) 
        : m_paramName(paramName), m_target(target) {}
    
    // === Point Management ===
    
    void addPoint(double beat, double value, float curve = 0.0f) {
        m_points.push_back({beat, value, curve});
        sortPoints();
    }
    
    void removePoint(size_t index) {
        if (index < m_points.size()) {
            m_points.erase(m_points.begin() + index);
        }
    }
    
    void clear() {
        m_points.clear();
    }
    
    // === Logic ===
    
    /**
     * @brief Interpolates the value at a specific timeline position (beat-space).
     */
    double getValueAtBeat(double beat) const {
        if (m_points.empty()) return m_defaultValue;
        if (beat <= m_points.front().beat) return m_points.front().value;
        if (beat >= m_points.back().beat) return m_points.back().value;
        
        // Find interval
        auto it = std::lower_bound(m_points.begin(), m_points.end(), beat, 
            [](const AutomationPoint& p, double b) { return p.beat < b; });
            
        if (it == m_points.begin()) return it->value;
        
        const auto& p1 = *(it - 1);
        const auto& p2 = *it;
        
        double t = (beat - p1.beat) / (p2.beat - p1.beat);
        
        // Piecewise Linear if curve is low, otherwise tension (v3.2)
        if (std::abs(p1.curve) > 0.001f) {
            // Apply tension. 
            // Formula: s = (exp(tension * t) - 1) / (exp(tension) - 1)
            // Clamp tension to avoid div by zero or extreme values [-10, 10]
            double tension = std::clamp(static_cast<double>(p1.curve) * 5.0, -10.0, 10.0);
            if (std::abs(tension) > 0.001) {
                 double denom = std::exp(tension) - 1.0;
                 if (std::abs(denom) > 1e-9) {
                     t = (std::exp(tension * t) - 1.0) / denom;
                 }
            }
        }
        
        return p1.value + (p2.value - p1.value) * t;
    }
    
    void sortPoints() {
        std::sort(m_points.begin(), m_points.end());
    }
    
    // === Properties ===
    
    const std::vector<AutomationPoint>& getPoints() const { return m_points; }
    std::vector<AutomationPoint>& getPoints() { return m_points; }
    
    void setTarget(const std::string& paramName) { m_paramName = paramName; }
    const std::string& getTarget() const { return m_paramName; }
    
    void setAutomationTarget(AutomationTarget target) { m_target = target; }
    AutomationTarget getAutomationTarget() const { return m_target; }

    void setDefaultValue(double val) { m_defaultValue = val; }
    double getDefaultValue() const { return m_defaultValue; }

    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

private:
    std::vector<AutomationPoint> m_points;
    std::string m_paramName;
    AutomationTarget m_target = AutomationTarget::None;
    double m_defaultValue = 0.0;
    bool m_visible = true;
};

} // namespace Audio
} // namespace Nomad
