/*
 * XWiimote
 *
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
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

#include "analog-stick-axis.h"


static void print_analog_stick_config (struct analog_stick_axis_config *config, InputInfoPtr info) {
  char low[30];
  char high[30];

  switch (config->low.type) {
    case FUNC_BTN:
      snprintf(low, sizeof(low), ", low-mapping=%d [button]", config->low.u.btn);
      break;
    case FUNC_KEY:
      snprintf(low, sizeof(low), ", high-mapping=%d [key]", config->low.u.key);
      break;
    default:
      low[0] = '\0';
      break;
  }

  switch (config->high.type) {
    case FUNC_BTN:
      snprintf(high, sizeof(high), ", high-mapping=%d [button]", config->high.u.btn);
      break;
    case FUNC_KEY:
      snprintf(high, sizeof(high), ", high-mapping=%d [key]", config->high.u.key);
      break;
    default:
      high[0] = '\0';
      break;
  }

  xf86Msg(X_INFO, "%s axis configured with mode=%d, deadzone=%d, amplify=%f%s%s\n", config->name, config->mode, config->deadzone, config->amplify, low, high);
}

void configure_analog_stick_axis(struct analog_stick_axis_config *config,
                                 char const *option_key,
                                 InputInfoPtr info)
{
	char const *c;
	char v[40];
	int i;
	double d;
  char key_name[100];
  char const *value;

	value = xf86FindOptionValue(info->options, option_key);
	if (!value)
		return;

	c = value;

	while (*c != '\0') {
		/* Skip any possible whitespace */
		while ((*c == ' ' || *c == '\t') && *c != '\0') c++;

		if (sscanf(c, "mode=%40s", v)) {
			if (strcmp(v, "relative") == 0) {
				config->mode = ANALOG_STICK_AXIS_MODE_RELATIVE;
			} else if (strcmp(v, "absolute") == 0) {
				config->mode = ANALOG_STICK_AXIS_MODE_ABSOLUTE;
			} else if (strcmp(v, "none") == 0) {
				config->mode = ANALOG_STICK_AXIS_MODE_NONE;
			} else {
				xf86Msg(X_WARNING, "%s: error parsing mode. value: %s\n", option_key, v);
			}
		} else if (sscanf(c, "keylow=%40s", v)) {
      snprintf(key_name, sizeof(key_name), "%s low", option_key);
			configure_key_by_value(&config->low, key_name, v, info);
		} else if (sscanf(c, "keyhigh=%40s", v)) {
      snprintf(key_name, sizeof(key_name), "%s high", option_key);
			configure_key_by_value(&config->high, key_name, v, info);
		} else if (sscanf(c, "deadzone=%i", &i)) {
			if (i > 1 && i < 100) {
				config->deadzone = i;
			} else {
				xf86Msg(X_WARNING, "%s: error parsing deadzone. value: %d\n", option_key, i);
			}
		} else if (sscanf(c, "amplify=%lf", &d)) {
			if (d >= 0.0  && d < 10.0) {
				config->amplify = d;
			} else {
				xf86Msg(X_WARNING, "%s: error parsing amplify. value: %f\n", option_key, d);
			}
		}

		/* Move past this parsed option */
		while (*c != ' ' && *c != '\t' && *c != '\0') c++;
	}

  print_analog_stick_config (config, info);
}


void handle_analog_stick_axis(struct analog_stick_axis *axis,
                              struct analog_stick_axis_config *config,
                              int32_t value,
                              int state,
                              InputInfoPtr info,
                              int first_valuator)
{
	int32_t pixel;

  if (abs(value) <= config->deadzone) {
    value = 0;
  }

	/* Set up scroll value */
	if (config->mode == ANALOG_STICK_AXIS_MODE_RELATIVE) {
	  axis->amplified = pow((value / config->deadzone), config->amplify);
    axis->previous_delta = axis->delta;
		axis->delta = (int) axis->amplified;
		axis->subpixel += axis->amplified - axis->delta;
		pixel = (int) axis->subpixel;
		if (pixel != 0) {
			axis->delta += pixel;
			axis->subpixel -= pixel;
		}
	} else if (config->mode == ANALOG_STICK_AXIS_MODE_ABSOLUTE) {
    if (value != 0) {
      if (value < 0) {
        value += config->deadzone;
      } else {
        value -= config->deadzone;
      }
      value *= (100 / config->deadzone);
    }
	  axis->amplified = value * config->amplify;
		axis->delta = axis->amplified - axis->previous_delta;
    axis->previous_delta = axis->amplified;
	} else {
		axis->delta = 0;
    axis->previous_delta = 0;
	}

	/* Handle key mappings */
	if (value > config->deadzone && axis->previous_value <= config->deadzone) {
    // Axis moved to high
		handle_key(&axis->high, &config->high, state, info);
		handle_key(&axis->low, &config->low, 0, info);
	}
	else if (value < -config->deadzone && axis->previous_value >= -config->deadzone) {
	  // Axis moved to low 
		handle_key(&axis->low, &config->low, 0, info);
		handle_key(&axis->low, &config->low, state, info);
	}
	else if (value >= -config->deadzone && value <= config->deadzone) {
	  // Axis moved to rest
		handle_key(&axis->low, &config->low, 0, info);
		handle_key(&axis->high, &config->high, 0, info);
	}

  /* Handle pointer motion */
  if (config->mode != ANALOG_STICK_AXIS_MODE_NONE) {
    xf86PostMotionEvent(info->dev, 0, first_valuator, 2 - first_valuator, axis->delta);
  }

  /* Give easy access to the state */
  if (axis->low.state) {
    axis->state = axis->low.state;
  }  else if (axis->high.state) {
    axis->state = axis->high.state;
  } else {
    axis->state = 0;
  }

	axis->previous_value = value;
}
