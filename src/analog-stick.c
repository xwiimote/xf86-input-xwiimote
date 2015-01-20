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

#include "analog-stick.h"



static int analog_stick_map_x_to_octegon(int x, int y) {
    double slope;
    double max_x;
    double real_x;
    double gradient_length;
    double gradient_slice;

    slope = y / x;

    if (slope > 1.0) {
      //scale changes for x
      max_x = y + (x/tan(ANALOG_STICK_SLICE_OUTER_ANGLE));

      gradient_length = 2 * (tan(ANALOG_STICK_SLICE_CENTER_ANGLE / 2) * max_x);
      gradient_slice = x / sin(ANALOG_STICK_SLICE_OUTER_ANGLE);

      real_x = gradient_slice / gradient_length * max_x;

    } else {
      //Map directly to to x axis
      real_x = x + (y / tan(ANALOG_STICK_SLICE_OUTER_ANGLE));
    }
    return real_x;
}

static int analog_stick_map_y_to_octegon(int x, int y) {
    return analog_stick_map_x_to_octegon(x, y);
}

static int analog_stick_map_x_to_circle(int x, int y) {
    //TODO
    return 0;
}

static int analog_stick_map_y_to_circle(int x, int y) {
    return analog_stick_map_x_to_circle(y, x);
}


/*
static struct analog_stick_func map_analog_stick_nunchuk_default[KEYSET_NUM] = {
	[KEYSET_NORMAL] = {
		.x = {
			.mode = ANALOG_STICK_MODE_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_D,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_A,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
		.y = {
			.mode = ANALOG_STICK_MODE_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_W,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_S,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
	},

	[KEYSET_IR] = {
		.x = {
			.mode = ANALOG_STICK_MODE_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_D,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_A,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
		.y = {
			.mode = ANALOG_STICK_MODE_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_W,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_S,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
	},
};

static struct analog_stick_func map_analog_stick_left_default[KEYSET_NUM] = {
	[KEYSET_NORMAL] = {
		.x = {
			.mode = ANALOG_STICK_MODE_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_D,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_A,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
		.y = {
			.mode = ANALOG_STICK_MODE_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_W,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_S,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
	},

	[KEYSET_IR] = {
		.x = {
			.mode = ANALOG_STICK_MODE_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_D,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_A,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
		.y = {
			.mode = ANALOG_STICK_MODE_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_W,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_S,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
	},
};

static struct analog_stick_func map_analog_stick_right_default[KEYSET_NUM] = {
	[KEYSET_NORMAL] = {
		.x = {
			.mode = ANALOG_STICK_MODE_RELATIVE,
			.map_high = {
				.type = FUNC_IGNORE,
			},
			.map_low = {
				.type = FUNC_IGNORE,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
		.y = {
			.mode = ANALOG_STICK_MODE_RELATIVE,
			.map_high = {
				.type = FUNC_IGNORE,
			},
			.map_low = {
				.type = FUNC_IGNORE,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
	},
	[KEYSET_IR] = {
		.x = {
			.mode = ANALOG_STICK_MODE_RELATIVE,
			.map_high = {
				.type = FUNC_IGNORE,
			},
			.map_low = {
				.type = FUNC_IGNORE,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
		.y = {
			.mode = ANALOG_STICK_MODE_RELATIVE,
			.map_high = {
				.type = FUNC_IGNORE,
			},
			.map_low = {
				.type = FUNC_IGNORE,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
	},
};
*/


void configure_analog_stick(struct analog_stick_config *config,
                            char const *name,
                            InputInfoPtr info)
{
	const char *value;
	char axis_name[100];
  int i;

  i = 0;
  while (name[i] != '\0' && i < sizeof(config->name) - 1) {
    config->name[i] = name[i];
    i++;
  }
  config->name[i] = '\0';

	if (!name)
		return;

	if (snprintf(axis_name, 100, "%sX", name) < 100) {
		value = xf86FindOptionValue(info->options, axis_name);
		configure_analog_stick_axis (&config->x, name, value, info);
	}

	if (snprintf(axis_name, 100, "%sY", name) < 100) {
		value = xf86FindOptionValue(info->options, axis_name);
		configure_analog_stick_axis (&config->y, name, value, info);
	}
}


void handle_analog_stick(struct analog_stick *stick,
                         struct analog_stick_config *config,
                         struct xwii_event *ev,
                         int stick_index,
                         int state,
                         InputInfoPtr info)
{
  int x;
  int y;

  int mapped_x;
  int mapped_y;

  x = ev->v.abs[stick_index].x;
  y = ev->v.abs[stick_index].y;

  switch(config->shape) {
    case ANALOG_STICK_SHAPE_CIRCLE:
      mapped_x = analog_stick_map_x_to_circle(x, y);
      mapped_y = analog_stick_map_y_to_circle(x, y);
      break;
    case ANALOG_STICK_SHAPE_OCTEGON:
      mapped_x = analog_stick_map_x_to_octegon(x, y);
      mapped_y = analog_stick_map_y_to_octegon(x, y);
      break;
    default:
      mapped_x = x;
      mapped_y = y;
      break;
  }

	handle_analog_stick_axis(&stick->x, &config->x, mapped_x, state, info, 0);		
	handle_analog_stick_axis(&stick->y, &config->y, mapped_y, state, info, 1);	   
}
