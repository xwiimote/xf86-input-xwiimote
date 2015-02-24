/*
 * XWiimote
 *
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
 * Copyright (c) 2015 Zachary Dovel<zakkudo2@gmail.com>
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

#include "util.h"
#include "accelerometer.h"

static void calculate_angle(struct accelerometer *accelerometer,
                            struct accelerometer_config *config,
                            struct xwii_event *ev,
                            InputInfoPtr info)
{
    double x, z;
    double angle = -1;
    x = accelerometer->accel_history_ev[0].x;
    z = accelerometer->accel_history_ev[0].z;

    if (x > 100 || x < -100 || z > 100 || z < -100) {
      return;
    }
    
    {
      // First quadrant
      if (z > 0 && x >= 0) {
          angle = x * (90.0 / 100.0);
      }
      //Second quadrant
      if (z <= 0 && x > 0) {
          angle = (fabs(z) * (90.0 / 100.0)) + 90.0;
      }
      //Third quadrant
      if (z < 0 && x <= 0) {
          angle = (fabs(x) * (90.0 / 100.0)) + 180.0;
      }
      //Forth quadrant
      if (z >= 0 && x < 0) {
          angle = (z * (90.0 / 100.0)) + 270.0;
      }

      while (angle >= 360.0) {
        angle -= 360.0;
      }

      angle = 360 - angle;
      if (accelerometer->angle != angle) {
        accelerometer->angle = angle;
      }
    }
}


void handle_accelerometer_event(struct accelerometer *accelerometer,
                                struct accelerometer_config *config,
                                struct xwii_event *ev,
                                InputInfoPtr info)
{
	int32_t x, y, r;
	int i;

	++accelerometer->accel_history_cur;
	accelerometer->accel_history_cur %= ACCELEROMETER_HISTORY_NUM;
	accelerometer->accel_history_ev[accelerometer->accel_history_cur] = ev->v.abs[0];

	/* choose the smallest one */
	x = accelerometer->accel_history_ev[0].x;
	y = accelerometer->accel_history_ev[0].y;
	for (i = 1; i < ACCELEROMETER_HISTORY_NUM; i++) {
		if (accelerometer->accel_history_ev[i].x < x)
			x = accelerometer->accel_history_ev[i].x;
		if (accelerometer->accel_history_ev[i].y < y)
			y = accelerometer->accel_history_ev[i].y;
	}

	/* limit values to make it more stable */
	r = x % ACCELEROMETER_HISTORY_MOD;
	x -= r;
	r = y % ACCELEROMETER_HISTORY_MOD;
	y -= r;

  calculate_angle(accelerometer, config, ev, info);
}


static void smooth_rotate(struct accelerometer *accelerometer,
                          struct accelerometer_config *config,
                          InputInfoPtr info) {
  double angle =  accelerometer->angle;
  double previous_angle = accelerometer->smooth_rotate_angle;
  double counter_clockwise_distance = 0.0;
  double clockwise_distance = 0.0;
  double distance = previous_angle - angle;

  if (distance > config->angle_deadzone) {
    accelerometer->is_in_deadzone  = FALSE;
  } else if (accelerometer->is_in_deadzone) {
    return;
  }

  if (distance > 0.0) {
    counter_clockwise_distance = fabs(distance);
    clockwise_distance = 360.0 - counter_clockwise_distance;
  } else if (distance < 0.0) {
    clockwise_distance = fabs(distance);
    counter_clockwise_distance = 360.0 - clockwise_distance;
  } else {
    clockwise_distance = distance;
    counter_clockwise_distance = distance;
  }

  if (clockwise_distance < counter_clockwise_distance) {
    if (clockwise_distance <= config->max_angle_delta) {
      previous_angle = angle;
    } else {
      previous_angle += config->max_angle_delta;
    }
  } else {
    if (counter_clockwise_distance <= config->max_angle_delta) {
       previous_angle = angle;
    } else {
      previous_angle -= config->max_angle_delta;
    }
  }

  if (previous_angle < 0.0) {
    previous_angle += 360.0;
  } else if (previous_angle > 360.0) {
    previous_angle -= 360.0;
  }

  if (previous_angle == angle) {
    accelerometer->is_in_deadzone = TRUE;
  }
  
  accelerometer->smooth_rotate_angle = previous_angle;
}


void handle_accelerometer_timer(struct accelerometer *accelerometer,
                                struct accelerometer_config *config,
                                InputInfoPtr info)
{
    smooth_rotate(accelerometer, config, info);
}


void configure_accelerometer(struct accelerometer_config *config, 
                             char const *prefix,
                             InputInfoPtr info)
{
	const char *t;
  char option_key[100];

  snprintf(option_key, sizeof(option_key), "%sAccelerometerMaxAngleDelta", prefix);
	t = xf86FindOptionValue(info->options, option_key);
  if (parse_double_with_default(t, &config->max_angle_delta, ACCELEROMETER_MAX_ANGLE_DELTA)) {
    xf86IDrvMsg(info, X_INFO, "%s %f\n", option_key, config->max_angle_delta);
  }

  snprintf(option_key, sizeof(option_key), "%sAccelerometerAngleDeadzone", prefix);
	t = xf86FindOptionValue(info->options, option_key);
  if (parse_double_with_default(t, &config->angle_deadzone, ACCELEROMETER_ANGLE_DEADZONE)) {
    xf86IDrvMsg(info, X_INFO, "%s %f\n", option_key, config->angle_deadzone);
  }
}
