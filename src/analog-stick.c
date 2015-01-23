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

void configure_analog_stick(struct analog_stick_config *config,
                            char const *name,
                            InputInfoPtr info)
{
	char axis_name[100];

	if (snprintf(axis_name, 100, "%sAnalogStickAxisX", name) < 100) {
		configure_analog_stick_axis (&config->x, axis_name, info);
	}

	if (snprintf(axis_name, 100, "%sAnalogStickAxisY", name) < 100) {
		configure_analog_stick_axis (&config->y, axis_name, info);
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

  /* Give easy access to the state */
  if (stick->x.state) {
    stick->state = stick->x.state;
  } else if (stick->y.state) {
    stick->state = stick->y.state;
  } else {
    stick->state = 0;
  }
}
