void FileBrowser::showHiddenBreadcrumbMenu(const std::vector<std::string>& hiddenPaths, const NUIPoint& position) {
    if (!popupMenu_ || hiddenPaths.empty()) return;

    popupMenu_->clear();
    
    // Header
    popupMenu_->addItem("Hidden Folders", [](){}); // Non-clickable header? Or just separator?
    popupMenu_->addSeparator();

    for (const auto& path : hiddenPaths) {
        std::filesystem::path p(path);
        std::string name = p.filename().string();
        if (name.empty()) name = p.string();
        
        popupMenu_->addItem(name, [this, path]() {
            navigateTo(path);
        });
    }

    popupMenu_->showAt(position);
}

bool FileBrowser::handleBreadcrumbMouseEvent(const NUIMouseEvent& event) {
    if (breadcrumbs_.empty() || breadcrumbBounds_.isEmpty()) return false;
    const float y = breadcrumbBounds_.y;
    const float h = breadcrumbBounds_.height;
    
    int hoveredIndex = -1;

    for (size_t i = 0; i < breadcrumbs_.size(); ++i) {
        const auto& crumb = breadcrumbs_[i];
        const float w = crumb.width > 0.0f ? crumb.width : static_cast<float>(crumb.name.size()) * 7.0f;
        if (event.position.x >= crumb.x && event.position.x <= crumb.x + w &&
            event.position.y >= y && event.position.y <= y + h) {
            
            hoveredIndex = static_cast<int>(i);

            if (event.pressed && event.button == NUIMouseButton::Left) {
                if (crumb.name == "...") {
                     // Show hidden folders menu
                     showHiddenBreadcrumbMenu(crumb.hiddenPaths, event.position);
                } else {
                     navigateToBreadcrumb(static_cast<int>(i));
                }
                return true;
            }
        }
    }
    
    if (hoveredIndex != hoveredBreadcrumbIndex_) {
        hoveredBreadcrumbIndex_ = hoveredIndex;
        // setDirty(true); // Redraw
    }
    
    return false;
}
