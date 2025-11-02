# Dropdown System Quick Reference
## NOMAD UI v2.0

**Quick start guide for the container-based dropdown system**

---

## 🚀 **Quick Start**

### **Basic Dropdown**
```cpp
auto dropdown = std::make_shared<NUIDropdown>();
dropdown->addItem("Option 1", 1);
dropdown->addItem("Option 2", 2);
parent->addChild(dropdown);
```

### **With Selection Callback**
```cpp
dropdown->setOnSelectionChanged([](int index, int value, const std::string& text) {
    std::cout << "Selected: " << text << std::endl;
});
```

---

## 🎨 **Styling**

### **Modern Theme Colors**
```cpp
// Semi-transparent background
dropdown->setBackgroundColor(themeManager.getColor("surface").withAlpha(0.95f));

// Soft hover overlay
dropdown->setHoverColor(NUIColor(120, 90, 255, 0.15f));

// Primary text for selected items
dropdown->setSelectedColor(themeManager.getColor("primary"));
```

### **Custom Colors**
```cpp
dropdown->setBackgroundColor(NUIColor::fromHex(0xff2a2a2e));
dropdown->setBorderColor(NUIColor::fromHex(0xff3a3a3e));
dropdown->setTextColor(NUIColor::fromHex(0xffE5E5E8));
```

---

## ⚙️ **Configuration**

### **Scrolling**
```cpp
// Enable scrolling for more than 5 items
dropdown->setMaxVisibleItems(5);
```

### **Placeholder Text**
```cpp
dropdown->setPlaceholderText("Choose an option...");
```

### **Item Visibility**
```cpp
// Hide specific items
dropdown->setItemVisible(2, false);

// Or create hidden items
auto item = std::make_shared<NUIDropdownItem>("Hidden", 0);
item->setVisible(false);
dropdown->addItem(item);
```

---

## 🎮 **Interactions**

### **Mouse Events**
- **Click button** → Toggle dropdown
- **Click item** → Select and close
- **Click outside** → Close dropdown
- **Hover item** → Highlight

### **Keyboard Navigation**
- **↑/↓** → Navigate items
- **Enter** → Select item
- **Escape** → Close dropdown

---

## 🔧 **Advanced Usage**

### **Multiple Independent Dropdowns**
```cpp
auto deviceDropdown = std::make_shared<NUIDropdown>();
auto sampleRateDropdown = std::make_shared<NUIDropdown>();

// Each manages its own container - no interference
deviceDropdown->addItem("SoundID Reference", 1);
sampleRateDropdown->addItem("48000 Hz", 48000);

parent->addChild(deviceDropdown);
parent->addChild(sampleRateDropdown);
```

### **Custom Item Management**
```cpp
// Create items with custom properties
auto item1 = std::make_shared<NUIDropdownItem>("Enabled Option", 1);
auto item2 = std::make_shared<NUIDropdownItem>("Disabled Option", 2);
item2->setEnabled(false);

dropdown->addItem(item1);
dropdown->addItem(item2);
```

### **Event Callbacks**
```cpp
dropdown->setOnOpen([]() {
    std::cout << "Dropdown opened" << std::endl;
});

dropdown->setOnClose([]() {
    std::cout << "Dropdown closed" << std::endl;
});

dropdown->setOnSelectionChanged([](int index, int value, const std::string& text) {
    std::cout << "Selected index " << index << ": " << text << " (value: " << value << ")" << std::endl;
});
```

---

## 🐛 **Troubleshooting**

### **Dropdown Not Appearing**
```cpp
// Check container initialization
if (dropdown->isOpen() && !container->isVisible()) {
    // Container not properly set up
}
```

### **Scrolling Not Working**
```cpp
// Verify max visible items is set
dropdown->setMaxVisibleItems(5); // Enable scrolling
```

### **Positioning Issues**
```cpp
// Ensure proper bounds are set
container->setSourceBounds(dropdown->getBounds());
container->setAvailableSpace(parent->getBounds());
```

---

## 📋 **Common Patterns**

### **Audio Device Selection**
```cpp
auto deviceDropdown = std::make_shared<NUIDropdown>();
deviceDropdown->setPlaceholderText("Select Audio Device");

// Add devices
for (const auto& device : audioDevices) {
    deviceDropdown->addItem(device.name, device.id);
}

deviceDropdown->setOnSelectionChanged([](int index, int value, const std::string& text) {
    audioManager.setDevice(value);
});
```

### **Sample Rate Selection**
```cpp
auto sampleRateDropdown = std::make_shared<NUIDropdown>();
sampleRateDropdown->setMaxVisibleItems(4); // Enable scrolling

std::vector<int> sampleRates = {44100, 48000, 88200, 96000};
for (int rate : sampleRates) {
    sampleRateDropdown->addItem(std::to_string(rate) + " Hz", rate);
}
```

### **Settings with Disabled Options**
```cpp
auto settingsDropdown = std::make_shared<NUIDropdown>();

// Add enabled options
auto option1 = std::make_shared<NUIDropdownItem>("Option 1", 1);
auto option2 = std::make_shared<NUIDropdownItem>("Option 2 (Coming Soon)", 2);
option2->setEnabled(false);

settingsDropdown->addItem(option1);
settingsDropdown->addItem(option2);
```

---

## ✅ **Best Practices**

1. **Use consistent styling** across all dropdowns
2. **Set appropriate maxVisibleItems** for scrolling
3. **Handle selection callbacks** properly
4. **Use placeholder text** for better UX
5. **Test with multiple dropdowns** to ensure isolation
6. **Consider keyboard navigation** for accessibility

---

**Quick Reference Version:** 2.0  
**Last Updated:** January 2025
