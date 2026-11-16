#pragma once

// Implemented in the static library and exported by plugin-entry.cpp, as clap-wrapper's clap-first layout expects.
bool ssp_entry_init(const char *pluginPath);
void ssp_entry_deinit(void);
const void *ssp_entry_get_factory(const char *factoryId);
