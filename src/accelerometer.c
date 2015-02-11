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
        xf86IDrvMsg(info, X_INFO, "accelerometer angle: (%f)\n", angle); 
        accelerometer->angle = angle;
      }
    }
}


void handle_accelerometer(struct accelerometer *accelerometer,
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


void configure_accelerometer(struct accelerometer_config *config, 
                             char const *name,
                             InputInfoPtr info)
{
}
