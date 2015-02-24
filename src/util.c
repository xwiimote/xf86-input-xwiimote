/*
 * XWiimote
 *
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
 * Copyright (c) 2015 Zachary Dovel <zakkudo2@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

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
