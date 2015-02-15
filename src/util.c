#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xorg-server.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <misc.h>

#include "util.h"


BOOL parse_int_with_default(char const *text, int *out, int default_value) {
    int value = 0;
    BOOL changed = FALSE;

    if (text) {
      value = atoi(text);
    } else {
      value = default_value;
    }

    if (*out != value) {
      *out = value;
      changed = TRUE;
    }
    return changed;
}


BOOL parse_double_with_default(char const *text, double *out, double default_value) {
    double value = 0.0;
    BOOL changed = FALSE;

    if (text) {
      value = atof(text);
    } else {
      value = default_value;
    }

    if (*out != value) {
      *out = value;
      changed = TRUE;
    }
    return changed;
}


BOOL parse_bool_with_default(char const *text, BOOL *out, BOOL default_value) {
    BOOL value = FALSE;
    BOOL changed = FALSE;

    if (text) {
      int i;
      if (!strcasecmp(text, "on") || !strcasecmp(text, "true") || !strcasecmp(text, "yes"))
        value = TRUE;
      else if (sscanf(text, "%i", &i) != 1)
        value = FALSE;
    } else {
      value = default_value;
    }

    if (*out != value) {
      *out = value;
      changed = TRUE;
    }
    return changed;
}
