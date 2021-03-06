#include <utils.h>
#include <glib.h>

#ifndef BD_LIB
#define BD_LIB

#include "plugins.h"

#define BD_INIT_ERROR bd_init_error_quark ()
typedef enum {
    BD_INIT_ERROR_PLUGINS_FAILED,
} BDInitError;

gboolean bd_init (BDPluginSpec **require_plugins, BDUtilsLogFunc log_func, GError **error);
gboolean bd_try_init (BDPluginSpec **require_plugins, BDUtilsLogFunc log_func, GError **error);
gboolean bd_reinit (BDPluginSpec **require_plugins, gboolean reload, BDUtilsLogFunc log_func, GError **error);
gboolean bd_is_initialized ();

#endif  /* BD_LIB */
