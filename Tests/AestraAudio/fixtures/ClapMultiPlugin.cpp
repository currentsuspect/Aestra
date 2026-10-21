// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// A CLAP bundle that holds three plugins, like LSP's holds about two hundred: the fixture for the helper's scan.
#include <clap/clap.h>

#include <cstring>

namespace {
const char* const kEffectFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr};
const char* const kSynthFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr};

const clap_plugin_descriptor_t kDescriptors[3] = {
    {CLAP_VERSION_INIT, "test.aestra.first", "First Effect", "Aestra Tests", nullptr, nullptr, nullptr, "1.0.0", "", kEffectFeatures},
    {CLAP_VERSION_INIT, "test.aestra.second", "Second Synth", "Aestra Tests", nullptr, nullptr, nullptr, "2.0.0", "", kSynthFeatures},
    {CLAP_VERSION_INIT, "test.aestra.third", "Third Effect", "Aestra Tests", nullptr, nullptr, nullptr, "3.0.0", "", kEffectFeatures},
};

uint32_t count(const clap_plugin_factory_t*) { return 3; }
const clap_plugin_descriptor_t* descriptor(const clap_plugin_factory_t*, uint32_t i) { return i < 3 ? &kDescriptors[i] : nullptr; }
const clap_plugin_t* create(const clap_plugin_factory_t*, const clap_host_t*, const char*) { return nullptr; } // scan only

const clap_plugin_factory_t kFactory = {count, descriptor, create};
bool init(const char*) { return true; }
void deinit() {}
const void* getFactory(const char* id) { return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr; }
} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, init, deinit, getFactory};
