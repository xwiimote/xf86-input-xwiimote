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
#include <math.h>
#include <xf86.h>

#include "motionplus.h"

static int32_t get_axis(struct motionplus_config *config,
                  			struct xwii_event *ev,
                  			unsigned int axis)
{
	switch (axis) {
	case 0:
		axis = config->x;
		break;
	case 1:
		axis = config->y;
		break;
	case 2:
		axis = config->z;
		break;
	default:
		return 0;
	}

	switch (axis) {
	case 0:
		return ev->v.abs[0].x * config->x_scale;
	case 1:
		return ev->v.abs[0].y * config->y_scale;
	case 2:
		return ev->v.abs[0].z * config->z_scale;
	default:
		return 0;
	}
}


static void parse_axis(const char *t,	unsigned int *out)
{
	if (!t)
		return;

	if (!strcasecmp(t, "x"))
		*out = 0;
	else if (!strcasecmp(t, "y"))
		*out = 1;
	else if (!strcasecmp(t, "z"))
		*out = 2;
}

void handle_motionplus_event(struct motionplus *motionplus,
                             struct motionplus_config *config,
                             struct xwii_event *ev,
                             InputInfoPtr info)
{
	int32_t x, z;

  x = get_axis(config, ev, 0) / 100;
  z = get_axis(config, ev, 2) / 100;
  xf86PostMotionEvent(info->dev, 0, 0, 2, x, z);
}


static void parse_scale(const char *t, int *out)
{
	if (!t)
		return;

	*out = atoi(t);
}


void configure_motionplus(struct motionplus_config *config,
                          char const *prefix,
                          InputInfoPtr info)
{
	const char *normalize, *factor, *t;
	int x, y, z, fac;
  char option_key[100];

	/* TODO: Allow modifying x, y, z and factor via xinput-properties for
	 * run-time calibration. */
  
  snprintf(option_key, sizeof(option_key), "%sCalibrationFactor", prefix);
	factor = xf86FindOptionValue(info->options, option_key);
	if (!factor)
		factor = "";

	if (!strcasecmp(factor, "on") ||
		!strcasecmp(factor, "true") ||
		!strcasecmp(factor, "yes"))
		config->factor = 50;
	else if (sscanf(factor, "%i", &fac) != 1)
		config->factor = 0;

  snprintf(option_key, sizeof(option_key), "%sNormalization", prefix);
	normalize = xf86FindOptionValue(info->options, option_key);
	if (!normalize)
		normalize = "";

	if (!strcasecmp(normalize, "on") ||
		!strcasecmp(normalize, "true") ||
		!strcasecmp(normalize, "yes")) {
    config->x_normalization = 0;
    config->y_normalization = 0;
    config->z_normalization = 0;
	} else if (sscanf(normalize, "%i:%i:%i", &x, &y, &z) == 3) {
    config->x_normalization = x;
    config->y_normalization = y;
    config->z_normalization = z;
	}

  snprintf(option_key, sizeof(option_key), "%sXAxis", prefix);
	t = xf86FindOptionValue(info->options, option_key);
	parse_axis(t, &config->x);

  snprintf(option_key, sizeof(option_key), "%sXScale", prefix);
	t = xf86FindOptionValue(info->options, option_key);
	parse_scale(t, &config->x_scale);

  snprintf(option_key, sizeof(option_key), "%sYAxis", prefix);
	t = xf86FindOptionValue(info->options, option_key);
	parse_axis(t, &config->y);

  snprintf(option_key, sizeof(option_key), "%sYScale", prefix);
	t = xf86FindOptionValue(info->options, option_key);
	parse_scale(t, &config->y_scale);

  snprintf(option_key, sizeof(option_key), "%sZAxis", prefix);
	t = xf86FindOptionValue(info->options, option_key);
	parse_axis(t, &config->z);

  snprintf(option_key, sizeof(option_key), "%sZScale", prefix);
	t = xf86FindOptionValue(info->options, option_key);
	parse_scale(t, &config->z_scale);
}


void preinit_motionplus(struct motionplus_config *config)
{
	config->x = 0;
	config->y = 1;
	config->z = 2;
	config->x_scale = 1;
	config->y_scale = 1;
	config->z_scale = 1;
}
