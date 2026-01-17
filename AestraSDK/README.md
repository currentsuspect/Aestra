# AestraSDK

**Plugin and extension API**

## Purpose

AestraSDK provides the interface for third-party plugins and extensions:
- Plugin host architecture
- VST3/AU/CLAP wrapper (future)
- Extension API for custom modules
- Scripting interface (future)

## Structure

```
AestraSDK/
├── include/
│   ├── PluginHost.h
│   ├── PluginInterface.h
│   └── Extension.h
└── src/
```

## Design Principles

- **Stable ABI** - Binary compatibility across versions
- **Sandboxed** - Plugins can't crash Aestra
- **Documented** - Every API call has examples
- **Versioned** - Clear deprecation policy

## Status

⚪ **Planned for v3.0** - Not yet started

## Vision

Allow developers to extend Aestra with:
- Custom audio effects
- New UI components
- Workflow automation
- Integration with external tools

---

*"Write for your future self."*
