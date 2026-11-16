#include <clap/clap.h>

#include "plugin-entry.h"

extern "C" {
CLAP_EXPORT const clap_plugin_entry clap_entry = {CLAP_VERSION_INIT, ssp_entry_init, ssp_entry_deinit,
						  ssp_entry_get_factory};
}
