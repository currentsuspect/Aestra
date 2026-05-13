# Long-Line Performance Analysis for NUITextInput

## Current Implementation Analysis

### CharX Generation Code (lines 58-82 in NUITextInput.cpp)

```cpp
// Build per-line character X positions (O(n) per line, not O(n²))
line.charX.clear();
line.charX.push_back(0.0f);  // INVARIANT: always at least position 0.0f

if (line.endIndex > line.startIndex)
{
    std::string lineText = text_.substr(line.startIndex, line.endIndex - line.startIndex);
    if (inputType_ == InputType::Password) {
        lineText = std::string(line.endIndex - line.startIndex, passwordCharacter_);
    }
    
    float cumulativeX = 0.0f;
    
    // Measure each character individually to avoid O(n²) substring remeasurement
    for (int j = line.startIndex; j < line.endIndex; ++j)
    {
        std::string singleChar = lineText.substr(j - line.startIndex, 1);
        auto size = renderer.measureText(singleChar, fontSize);
        cumulativeX += size.width;
        line.charX.push_back(cumulativeX);
    }
}
```

## Performance Issues

### 1. Per-Character Measurement Overhead
- **Current**: Calls `renderer.measureText()` for every single character
- **Impact**: Each measurement may involve font lookup, glyph cache access, rasterization
- **Complexity**: O(n) renderer calls per line, where each call has constant but significant overhead

### 2. String Allocation Overhead
- **Current**: Creates a new `std::string` for each character via `substr(j - line.startIndex, 1)`
- **Impact**: n heap allocations per line
- **Complexity**: O(n) allocations per line

### 3. Real-World Impact Scenarios

#### Giant JSON Lines
```json
{"data":[{"id":1,"name":"test","value":123.45,"active":true,"tags":["a","b","c"]},{"id":2,...}]}
```
- Single line: ~200+ characters
- 200+ renderer.measureText() calls
- 200+ heap allocations

#### Minified JavaScript
```javascript
function(a,b,c){return d?e(f,g):h(i,j,k)?l(m,n):o(p,q)}
```
- Single line: ~100+ characters
- Complex glyph combinations (brackets, operators, letters)
- 100+ renderer.measureText() calls

#### Generated Code
```cpp
template<typename T,typename U,typename V,typename W,typename X>struct Complex{...};
```
- Single line: ~150+ characters
- Template-heavy content
- 150+ renderer.measureText() calls

## Performance Estimates

### Conservative Assumptions
- `renderer.measureText()` overhead: ~1-5μs per call (font lookup, cache access)
- String allocation overhead: ~0.5-1μs per allocation
- Total per character: ~1.5-6μs

### Example Calculations

#### 200-character JSON line
- 200 × 1.5μs = 300μs (best case)
- 200 × 6μs = 1200μs (worst case)
- **Result**: 0.3-1.2ms per line layout update

#### 1000-character generated code line
- 1000 × 1.5μs = 1500μs (best case)
- 1000 × 6μs = 6000μs (worst case)
- **Result**: 1.5-6ms per line layout update

### Impact on User Experience
- **Typing**: Each keystroke triggers layout update
- **1000-char line**: 1.5-6ms latency per keystroke
- **200-char line**: 0.3-1.2ms latency per keystroke
- **Threshold**: >16ms (60fps) becomes noticeable, >50ms feels sluggish

## Optimization Opportunities

### 1. Batch Measurement
**Approach**: Measure entire line at once, then compute cumulative positions
```cpp
// Measure entire line once
auto size = renderer.measureText(lineText, fontSize);

// This requires renderer to support per-character width extraction
// If not available, current approach is necessary
```

**Pros**: Single renderer call per line
**Cons**: Requires renderer API changes

### 2. Character Width Cache
**Approach**: Cache per-character widths globally
```cpp
static std::unordered_map<char, float> charWidthCache;

float getCharWidth(char c, float fontSize) {
    auto key = std::make_pair(c, fontSize);
    auto it = charWidthCache.find(key);
    if (it != charWidthCache.end()) {
        return it->second;
    }
    float width = renderer.measureText(std::string(1, c), fontSize).width;
    charWidthCache[key] = width;
    return width;
}
```

**Pros**: Eliminates repeated measurements for common characters
**Cons**: Cache invalidation on font/size changes, memory overhead

### 3. Monospace Optimization
**Approach**: Detect monospace fonts, use constant width
```cpp
if (isMonospaceFont(fontSize)) {
    float charWidth = renderer.measureText("M", fontSize).width;
    for (int j = line.startIndex; j < line.endIndex; ++j) {
        cumulativeX += charWidth;
        line.charX.push_back(cumulativeX);
    }
}
```

**Pros**: Massive speedup for code editors
**Cons**: Requires font detection, only helps for monospace

### 4. Incremental Layout
**Approach**: Only recompute changed regions
```cpp
// Track which lines actually changed
// Only update charX for modified lines
```

**Pros**: Avoids full recomputation
**Cons**: Complex invalidation logic, may not help for single-line changes

### 5. Lazy Measurement
**Approach**: Defer charX computation until actually needed
```cpp
// Only compute charX when:
// - Hit testing occurs
// - Caret moves
// - Selection is drawn
```

**Pros**: Avoids work for invisible/unused lines
**Cons**: May cause jank during interaction

## Recommendations

### Immediate (No API Changes)
1. **Profile actual performance**: Measure real-world `renderer.measureText()` overhead
2. **Add measurement timing**: Log layout update durations
3. **Set performance budget**: Target <5ms per line update

### Short-term (Minimal API Changes)
1. **Character width cache**: Implement simple cache for common characters
2. **Monospace detection**: Add font metadata query, optimize for code
3. **Lazy measurement**: Defer charX for off-screen lines

### Long-term (API Changes)
1. **Batch measurement API**: Add `renderer.measureTextWidths(text, fontSize)` returning array
2. **Incremental layout**: Track dirty regions, partial updates
3. **GPU acceleration**: Offload measurement to GPU if possible

## Conclusion

The current implementation is **functionally correct** but **not performance-optimized** for long lines. The O(n) per-line complexity with constant overhead per character becomes noticeable for lines >200 characters.

**Priority**: Medium. Current implementation is acceptable for typical text (emails, documents, UI labels) but will become problematic for:
- Code editors with minified/generated code
- JSON/XML processing
- Log file viewers
- Data-heavy applications

**Next Steps**: Profile with real renderer to get actual measurements, then implement character width cache as first optimization.
