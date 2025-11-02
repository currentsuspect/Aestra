# NOMAD UI Dropdown System v2.0
## Container-Based Architecture

**Version:** 2.0  
**Date:** January 2025  
**Status:** Production Ready  

---

## 🎯 **Overview**

The NOMAD UI Dropdown System v2.0 introduces a revolutionary container-based architecture that provides **complete isolation**, **scrollable content**, and **space-aware positioning** for dropdown components. This system eliminates dropdown interference, supports unlimited content, and adapts intelligently to available screen space.

### **Key Features**
- ✅ **Self-Contained Components** - Each dropdown list is completely isolated
- ✅ **Scrollable Content** - Automatic scrolling when content exceeds available space
- ✅ **Space-Aware Positioning** - Intelligent positioning above/below based on available space
- ✅ **Independent Operation** - Multiple dropdowns work without interference
- ✅ **Professional UX** - Modern DAW-style behavior and animations

---

## 🏗️ **Architecture**

### **Component Hierarchy**

```
NUIDropdown (Main Button Component)
├── Renders dropdown button + arrow
├── Handles button clicks and focus
├── Manages open/close state
└── Controls NUIDropdownContainer

NUIDropdownContainer (List Component)
├── Renders dropdown list + scrollbar
├── Handles item selection + hover
├── Manages scrolling + positioning
├── Processes keyboard navigation
└── Completely self-contained
```

### **Separation of Concerns**

| Component | Responsibility |
|-----------|----------------|
| **NUIDropdown** | Button rendering, open/close logic, focus management |
| **NUIDropdownContainer** | List rendering, item interaction, scrolling, positioning |

---

## 📋 **API Reference**

### **NUIDropdown Class**

#### **Constructor**
```cpp
NUIDropdown();
```

#### **Item Management**
```cpp
void addItem(const std::string& text, int value = 0);
void addItem(std::shared_ptr<NUIDropdownItem> item);
void removeItem(int index);
void clearItems();
void setItems(const std::vector<std::shared_ptr<NUIDropdownItem>>& items);

// Item visibility
void setItemVisible(int index, bool visible);
bool isItemVisible(int index) const;
```

#### **Selection Management**
```cpp
void setSelectedIndex(int index);
int getSelectedIndex() const;
std::shared_ptr<NUIDropdownItem> getSelectedItem() const;
std::string getSelectedText() const;
int getSelectedValue() const;
```

#### **Behavior Control**
```cpp
void setOpen(bool open);
bool isOpen() const;
void setMaxVisibleItems(int count);
int getMaxVisibleItems() const;
void setPlaceholderText(const std::string& text);
```

#### **Styling**
```cpp
void setBackgroundColor(const NUIColor& color);
void setBorderColor(const NUIColor& color);
void setTextColor(const NUIColor& color);
void setHoverColor(const NUIColor& color);
void setSelectedColor(const NUIColor& color);
void setArrowColor(const NUIColor& color);
```

#### **Callbacks**
```cpp
void setOnSelectionChanged(std::function<void(int index, int value, const std::string& text)> callback);
void setOnOpen(std::function<void()> callback);
void setOnClose(std::function<void()> callback);
```

---

### **NUIDropdownContainer Class**

#### **Constructor**
```cpp
NUIDropdownContainer();
```

#### **Content Management**
```cpp
void setItems(const std::vector<std::shared_ptr<NUIDropdownItem>>& items);
void setSelectedIndex(int index);
void setHoveredIndex(int index);
void setMaxVisibleItems(int count);
```

#### **Positioning & Sizing**
```cpp
void setSourceBounds(const NUIRect& bounds);
void setAvailableSpace(const NUIRect& space);
void updatePosition();
```

#### **Styling**
```cpp
void setBackgroundColor(const NUIColor& color);
void setBorderColor(const NUIColor& color);
void setTextColor(const NUIColor& color);
void setHoverColor(const NUIColor& color);
void setSelectedColor(const NUIColor& color);
```

#### **Callbacks**
```cpp
void setOnItemSelected(std::function<void(int index, int value, const std::string& text)> callback);
void setOnClose(std::function<void()> callback);
```

---

### **NUIDropdownItem Class**

#### **Constructor**
```cpp
NUIDropdownItem(const std::string& text = "", int value = 0);
```

#### **Properties**
```cpp
void setText(const std::string& text);
std::string getText() const;
void setValue(int value);
int getValue() const;
void setEnabled(bool enabled);
bool isEnabled() const;
void setVisible(bool visible);
bool isVisible() const;
```

---

## 🎨 **Visual Design System**

### **Modern Styling**

The dropdown system uses a **modern, translucent design** with:

- **Semi-transparent backgrounds** (`withAlpha(0.95f)`)
- **Soft drop shadows** for elevation
- **Rounded corners** (6px for container, 4px for items)
- **Vector-style arrows** instead of ASCII characters
- **Smooth animations** for open/close transitions

### **Color Scheme**

```cpp
// Container
backgroundColor_ = themeManager.getColor("surface").withAlpha(0.95f);
borderColor_ = themeManager.getColor("borderSubtle").withAlpha(0.6f);

// Items
hoverColor_ = NUIColor(120, 90, 255, 0.15f);    // Soft purple overlay
selectedColor_ = themeManager.getColor("primary"); // Primary text color
textColor_ = themeManager.getColor("textPrimary");

// Arrow
arrowColor_ = themeManager.getColor("textSecondary");
```

### **Typography**

- **Font Size:** 14px
- **Font Weight:** Regular
- **Selected Items:** Primary color (no background highlight)
- **Hover Items:** Soft purple overlay background

---

## ⚙️ **Scrolling System**

### **Automatic Scrolling**

The container automatically enables scrolling when:
- Content exceeds `maxVisibleItems` limit
- Available space is insufficient

### **Scrollbar Features**

- **Visual scrollbar** with semi-transparent track
- **Draggable thumb** showing scroll position
- **Auto-scroll** to keep selected items visible
- **Keyboard navigation** with arrow keys

### **Scrollbar Styling**

```cpp
// Track
NUIColor trackColor = NUIColor(0, 0, 0, 0.2f);
renderer.fillRoundedRect(trackBounds, 3.0f, trackColor);

// Thumb
NUIColor thumbColor = NUIColor(255, 255, 255, 0.6f);
renderer.fillRoundedRect(thumbBounds, 3.0f, thumbColor);
```

---

## 🎯 **Space-Aware Positioning**

### **Intelligent Positioning Algorithm**

1. **Check space below** the dropdown button
2. **Fall back to above** if insufficient space below
3. **Adjust height** to fit available space
4. **Prevent overflow** outside screen boundaries
5. **Respect other UI elements**

### **Positioning Logic**

```cpp
// Calculate position below source bounds
float dropdownY = sourceBounds_.y + sourceBounds_.height;

// Check if there's enough space below
if (dropdownY + dropdownHeight > availableSpace_.y + availableSpace_.height) {
    // Not enough space below, try above
    dropdownY = sourceBounds_.y - dropdownHeight;
    
    // If still not enough space, reduce height
    if (dropdownY < availableSpace_.y) {
        dropdownY = availableSpace_.y;
        dropdownHeight = sourceBounds_.y - availableSpace_.y;
    }
}
```

---

## 🎮 **Interaction System**

### **Mouse Events**

| Action | Behavior |
|--------|----------|
| **Click Button** | Toggle dropdown open/close |
| **Click Item** | Select item and close dropdown |
| **Click Outside** | Close dropdown |
| **Hover Item** | Highlight item with soft overlay |

### **Keyboard Navigation**

| Key | Behavior |
|-----|----------|
| **↑/↓** | Navigate through items |
| **Enter** | Select highlighted item |
| **Escape** | Close dropdown |
| **Tab** | Move focus to next component |

### **Focus Management**

- **Auto-focus** when dropdown opens
- **Release focus** when dropdown closes
- **Focus trapping** within dropdown when open

---

## 🔧 **Implementation Examples**

### **Basic Usage**

```cpp
// Create dropdown
auto dropdown = std::make_shared<NUIDropdown>();

// Add items
dropdown->addItem("Option 1", 1);
dropdown->addItem("Option 2", 2);
dropdown->addItem("Option 3", 3);

// Set selection callback
dropdown->setOnSelectionChanged([](int index, int value, const std::string& text) {
    std::cout << "Selected: " << text << " (value: " << value << ")" << std::endl;
});

// Add to parent component
parent->addChild(dropdown);
```

### **Advanced Configuration**

```cpp
// Create dropdown with custom styling
auto dropdown = std::make_shared<NUIDropdown>();

// Configure behavior
dropdown->setMaxVisibleItems(5);
dropdown->setPlaceholderText("Choose an option...");

// Custom styling
dropdown->setBackgroundColor(NUIColor::fromHex(0xff2a2a2e));
dropdown->setHoverColor(NUIColor(120, 90, 255, 0.2f));
dropdown->setSelectedColor(NUIColor::fromHex(0xff8B7FFF));

// Add items with visibility control
auto item1 = std::make_shared<NUIDropdownItem>("Visible Option", 1);
auto item2 = std::make_shared<NUIDropdownItem>("Hidden Option", 2);
item2->setVisible(false);

dropdown->addItem(item1);
dropdown->addItem(item2);

// Set callbacks
dropdown->setOnOpen([]() {
    std::cout << "Dropdown opened" << std::endl;
});

dropdown->setOnClose([]() {
    std::cout << "Dropdown closed" << std::endl;
});
```

### **Multiple Independent Dropdowns**

```cpp
// Create multiple dropdowns - they work independently
auto deviceDropdown = std::make_shared<NUIDropdown>();
auto sampleRateDropdown = std::make_shared<NUIDropdown>();
auto bufferSizeDropdown = std::make_shared<NUIDropdown>();

// Each dropdown manages its own container
deviceDropdown->addItem("SoundID Reference", 1);
deviceDropdown->addItem("ASIO Driver", 2);

sampleRateDropdown->addItem("44100 Hz", 44100);
sampleRateDropdown->addItem("48000 Hz", 48000);
sampleRateDropdown->addItem("96000 Hz", 96000);

// No interference between dropdowns
parent->addChild(deviceDropdown);
parent->addChild(sampleRateDropdown);
parent->addChild(bufferSizeDropdown);
```

---

## 🚀 **Performance Characteristics**

### **Memory Usage**

- **Container Creation:** Only when dropdown opens
- **Container Destruction:** When dropdown closes
- **Memory Overhead:** Minimal - containers are lightweight

### **Rendering Performance**

- **Batched Rendering:** Uses `renderer.flush()` for optimal GPU usage
- **Layered Rendering:** Proper Z-order with `NUILayer::Dropdown`
- **Efficient Scrolling:** Only renders visible items

### **Event Processing**

- **Event Isolation:** Each container handles its own events
- **No Event Bleeding:** Containers don't interfere with each other
- **Efficient Hit Testing:** Optimized bounds checking

---

## 🔍 **Debugging & Troubleshooting**

### **Common Issues**

#### **Dropdown Not Appearing**
- Check if `container_` is properly initialized
- Verify `setVisible(true)` is called on container
- Ensure proper positioning with `setSourceBounds()`

#### **Scrolling Not Working**
- Verify `maxVisibleItems` is set correctly
- Check if content exceeds visible area
- Ensure `isScrollable_` flag is set

#### **Positioning Issues**
- Verify `setAvailableSpace()` is called with correct bounds
- Check `updatePosition()` is called after bounds change
- Ensure parent component doesn't clip children

### **Debug Output**

```cpp
// Enable debug logging
dropdown->setOnOpen([]() {
    std::cout << "Dropdown opened - container visible: " 
              << container->isVisible() << std::endl;
});
```

---

## 📈 **Future Enhancements**

### **Planned Features**

- **Mouse wheel scrolling** support
- **Touch gesture** support for mobile
- **Custom item rendering** with icons
- **Search/filter** functionality
- **Multi-select** support
- **Animation easing** curves

### **Extensibility**

The container-based architecture makes it easy to:
- Add new interaction patterns
- Implement custom item types
- Create specialized dropdown variants
- Integrate with other UI systems

---

## ✅ **Migration Guide**

### **From v1.0 to v2.0**

The new system is **backward compatible** - existing code continues to work:

```cpp
// Old code (still works)
auto dropdown = std::make_shared<NUIDropdown>();
dropdown->addItem("Option 1", 1);

// New features available
dropdown->setMaxVisibleItems(10);  // Enable scrolling
// Container is automatically managed
```

### **Breaking Changes**

- **None** - Full backward compatibility maintained
- **New Features** - All new functionality is opt-in

---

## 🎉 **Conclusion**

The NOMAD UI Dropdown System v2.0 represents a **major architectural improvement** that provides:

- **Complete isolation** between dropdown instances
- **Professional scrolling** for unlimited content
- **Intelligent positioning** that adapts to available space
- **Modern visual design** with smooth animations
- **Zero breaking changes** from v1.0

This system is **production-ready** and provides the foundation for complex UI interactions in the NOMAD DAW application.

---

**Documentation Version:** 2.0  
**Last Updated:** January 2025  
**Maintainer:** NOMAD UI Team
