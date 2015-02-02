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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <xorg-server.h>
#include <xf86.h>
#include <xf86_OSproc.h>

#include "ir.h"


BOOL ir_is_active(struct ir *ir,
                  struct ir_config *config,
                  struct xwii_event *ev)
{
	return (ev->time.tv_sec < ir->last_valid_event.tv_sec + config->keymap_expiry_secs
			|| (ev->time.tv_sec == ir->last_valid_event.tv_sec + config->keymap_expiry_secs
				&& ev->time.tv_usec < ir->last_valid_event.tv_usec));
}

static void calculate_ir_coordinates(struct ir *ir,
                                     struct ir_config *config,
                                     struct xwii_event *ev,
                                     InputInfoPtr info)
{
	struct xwii_event_abs *a, *b, *c, d = {0};
	int i, dists[6];

	/* Grab first two valid points */
	a = b = NULL;
	for (i = 0; i < 4; ++i) {
		c = &ev->v.abs[i];
		if (xwii_event_ir_is_valid(c) && (c->x || c->y)) {
			if (!a) {
				a = c;
			} else if (!b) {
				b = c;
			} else {
				/* This may be a noisy point. Keep the two points that are
				 * closest to the reference points. */
				d.x = ir->ref_x + ir->vec_x;
				d.y = ir->ref_y + ir->vec_y;
				dists[0] = IR_DISTSQ(c->x, c->y, ir->ref_x, ir->ref_y);
				dists[1] = IR_DISTSQ(c->x, c->y, d.x, d.y);
				dists[2] = IR_DISTSQ(a->x, a->y, ir->ref_x, ir->ref_y);
				dists[3] = IR_DISTSQ(a->x, a->y, d.x, d.y);
				dists[4] = IR_DISTSQ(b->x, b->y, ir->ref_x, ir->ref_y);
				dists[5] = IR_DISTSQ(b->x, b->y, d.x, d.y);
				if (dists[1] < dists[0]) dists[0] = dists[1];
				if (dists[3] < dists[2]) dists[2] = dists[3];
				if (dists[5] < dists[4]) dists[4] = dists[5];
				if (dists[0] < dists[2]) {
					if (dists[4] < dists[2]) {
						a = c;
					} else {
						b = c;
					}
				} else if (dists[0] < dists[4]) {
					b = c;
				}
			}
		}
	}
	if (!a)
		return;

	if (!b) {
		/* Generate the second point based on historical data */
		b = &d;
		b->x = a->x - ir->vec_x;
		b->y = a->y - ir->vec_y;
		if (IR_DISTSQ(a->x, a->y, ir->ref_x, ir->ref_y)
				< IR_DISTSQ(b->x, b->y, ir->ref_x, ir->ref_y)) {
			b->x = a->x + ir->vec_x;
			b->y = a->y + ir->vec_y;
			ir->ref_x = a->x;
			ir->ref_y = a->y;
		} else {
			ir->ref_x = b->x;
			ir->ref_y = b->y;
		}
	} else {
		/* Record some data in case one of the points disappears */
		ir->vec_x = b->x - a->x;
		ir->vec_y = b->y - a->y;
		ir->ref_x = a->x;
		ir->ref_y = a->y;
	}

	/* Final point is the average of both points */
	a->x = (a->x + b->x) / 2;
	a->y = (a->y + b->y) / 2;

	/* Start averaging if the location is consistant */
	ir->avg_x = (ir->avg_x * ir->avg_count + a->x) / (ir->avg_count+1);
	ir->avg_y = (ir->avg_y * ir->avg_count + a->y) / (ir->avg_count+1);
	if (++ir->avg_count > config->avg_max_samples)
		ir->avg_count = config->avg_max_samples;
	if (IR_DISTSQ(a->x, a->y, ir->avg_x, ir->avg_y)
			< config->avg_radius * config->avg_radius) {
		if (ir->avg_count >= config->avg_min_samples) {
			a->x = (a->x + ir->avg_x * config->avg_weight) / (config->avg_weight+1);
			a->y = (a->y + ir->avg_y * config->avg_weight) / (config->avg_weight+1);
		}
	} else {
		ir->avg_count = 0;
	}

  ir->x = IR_MAX_X - a->x;

  if (ir->x < IR_MIN_X) {
    ir->x = IR_MIN_X;
  } else if (ir->x > IR_MAX_X) {
    ir->x = IR_MAX_X;
  }
  
  ir->y = a->y;

  if (ir->y < IR_MIN_Y) {
    ir->y = IR_MIN_Y;
  } else if (ir->y > IR_MAX_Y) {
    ir->y = IR_MAX_Y;
  }

	ir->last_valid_event = ev->time;
}


void handle_continuous_scrolling(struct ir *ir,
                                 struct ir_config *config,
                                 struct xwii_event *ev,
                                 InputInfoPtr info)
{
  double x_scale, y_scale;
  double x, y;

  if (ir->mode != IR_MODE_GAME) return;

  /* X */
  x = ir->smooth_scroll_x;
  x_scale = (double) config->continuous_scroll_max_x / (double) config->continuous_scroll_border_x;
  if (x < config->continuous_scroll_border_x) {
    ir->continuous_scroll_speed_x = (x - config->continuous_scroll_border_x) * x_scale;
  } else if (x > IR_MAX_X - config->continuous_scroll_border_x) {
    ir->continuous_scroll_speed_x = (x - (IR_MAX_X - config->continuous_scroll_border_x)) * x_scale;
  } else {
    ir->continuous_scroll_speed_x = 0;
  }

  /* Y */
  y = ir->smooth_scroll_y;
  y_scale = (double) config->continuous_scroll_max_y / (double) config->continuous_scroll_border_y;
  if (y < config->continuous_scroll_border_y) {
    ir->continuous_scroll_speed_y = (y - config->continuous_scroll_border_y) * y_scale;
  } else if (y > IR_MAX_Y - config->continuous_scroll_border_y) {
    ir->continuous_scroll_speed_y = (y - (IR_MAX_Y - config->continuous_scroll_border_y)) * y_scale;
  } else {
    ir->continuous_scroll_speed_y = 0;
  }
}


static CARD32
updateCursorPositionTimer(OsTimerPtr        timer,
                          CARD32            atime,
                          pointer           arg)
{
  struct ir *ir;
  int sigstate;
  int x, y;
  double delta_x, delta_y, delta_h, ratio;

  ir = arg;

  sigstate = xf86BlockSIGIO();

  //Handle the pointer position

  {
    double MAX_DELTA;

    if (ir->mode == IR_MODE_GAME) {
      MAX_DELTA = 8;
    } else {
      MAX_DELTA = 3;
    }

    ir->previous_smooth_scroll_x = ir->smooth_scroll_x;
    ir->previous_smooth_scroll_y = ir->smooth_scroll_y;

    delta_x = ((double) ir->x - (double) ir->previous_smooth_scroll_x);
    delta_y = ((double) ir->y - (double) ir->previous_smooth_scroll_y);

    if (delta_x != 0 || delta_y != 0) {
      delta_h = sqrt(pow(delta_x, 2) + pow(delta_y, 2));
      ratio = MAX_DELTA / delta_h;
      if (ratio < 1) {
        delta_x *= ratio;
        delta_y *= ratio;
      }
      ir->smooth_scroll_x = ir->previous_smooth_scroll_x + delta_x;
      ir->smooth_scroll_y = ir->previous_smooth_scroll_y + delta_y;

      if (ir->mode == IR_MODE_GAME) {
        xf86PostMotionEvent(ir->info->dev, Relative, 0, 2, (int) delta_x, (int) delta_y);
      } else {
        xf86PostMotionEvent(ir->info->dev, Absolute, 0, 2, (int) ir->smooth_scroll_x * IR_TO_SCREEN_RATIO, (int) ir->smooth_scroll_y * IR_TO_SCREEN_RATIO);
      }
    }  
  }

  //Handle the continuous edge scrolling
  if (ir->mode == IR_MODE_GAME) {
    ir->continuous_scroll_subpixel_x += ir->continuous_scroll_speed_x;
    x = (int) ir->continuous_scroll_subpixel_x;
    ir->continuous_scroll_subpixel_x -= x; 

    ir->continuous_scroll_subpixel_y += ir->continuous_scroll_speed_y;
    y = (int) ir->continuous_scroll_subpixel_y;
    ir->continuous_scroll_subpixel_y -= y; 

    if (x || y) {
      xf86PostMotionEvent(ir->info->dev, Relative, 0, 2, (int) (x), (int) (y));
    }
  }

  xf86UnblockSIGIO (sigstate);

  return 1;
}


void handle_ir(struct ir *ir,
               struct ir_config *config,
               struct xwii_event *ev,
               InputInfoPtr info)
{
  calculate_ir_coordinates(ir, config, ev, info);
  handle_continuous_scrolling (ir, config, ev, info); 

  if (!ir->timer) {
    ir->info = info;
    ir->timer = TimerSet(
          ir->timer,
          0,         
          1,         
          updateCursorPositionTimer,
          ir);
  }
}


static void parse_scale(const char *t, int *out)
{
	if (!t)
		return;

	*out = atoi(t);
}


void configure_ir(struct ir_config *config, 
                  char const *prefix,
                  InputInfoPtr info)
{
	const char *t;
  char option_key[100];

  snprintf(option_key, sizeof(option_key), "%sIRAvgRadius", prefix);
	t = xf86FindOptionValue(info->options, option_key);
	parse_scale(t, &config->avg_radius);
  xf86IDrvMsg(info, X_INFO, "%s %d\n", option_key, config->avg_radius);

  snprintf(option_key, sizeof(option_key), "%sIRAvgMaxSamples", prefix);
	t = xf86FindOptionValue(info->options, option_key);
	parse_scale(t, &config->avg_max_samples);
	if (config->avg_max_samples < 1) config->avg_max_samples = 1;
  xf86IDrvMsg(info, X_INFO, "%s %d\n", option_key, config->avg_max_samples);

  snprintf(option_key, sizeof(option_key), "%sIRAvgMinSamples", prefix);
	t = xf86FindOptionValue(info->options, option_key);
	parse_scale(t, &config->avg_min_samples);
	if (config->avg_min_samples < 1) {
		config->avg_min_samples = 1;
	} else if (config->avg_min_samples > config->avg_max_samples) {
		config->avg_min_samples = config->avg_max_samples;
	}
  xf86IDrvMsg(info, X_INFO, "%s %d\n", option_key, config->avg_min_samples);

  snprintf(option_key, sizeof(option_key), "%sIRAvgWeight", prefix);
	t = xf86FindOptionValue(info->options, option_key);
	parse_scale(t, &config->avg_weight);
	if (config->avg_weight < 0) config->avg_weight = 0;
  xf86IDrvMsg(info, X_INFO, "%s %d\n", option_key, config->avg_weight);

  snprintf(option_key, sizeof(option_key), "%sIRKeymapExpirySecs", prefix);
	t = xf86FindOptionValue(info->options, option_key);
	parse_scale(t, &config->keymap_expiry_secs);
  xf86IDrvMsg(info, X_INFO, "%s %d\n", option_key, config->keymap_expiry_secs);

  snprintf(option_key, sizeof(option_key), "%sIRContinuousScrollBorderX", prefix);
	t = xf86FindOptionValue(info->options, option_key);
	parse_scale(t, &config->continuous_scroll_border_x);
  xf86IDrvMsg(info, X_INFO, "%s %d\n", option_key, config->continuous_scroll_border_x);

  snprintf(option_key, sizeof(option_key), "%sIRContinuousScrollBorderY", prefix);
	t = xf86FindOptionValue(info->options, option_key);
	parse_scale(t, &config->continuous_scroll_border_y);
  xf86IDrvMsg(info, X_INFO, "%s %d\n", option_key, config->continuous_scroll_border_y);

  snprintf(option_key, sizeof(option_key), "%sIRContinuousScrollMaxX", prefix);
	t = xf86FindOptionValue(info->options, option_key);
	parse_scale(t, &config->continuous_scroll_max_x);
  xf86IDrvMsg(info, X_INFO, "%s %d\n", option_key, config->continuous_scroll_max_x);

  snprintf(option_key, sizeof(option_key), "%sIRContinuousScrollMaxY", prefix);
	t = xf86FindOptionValue(info->options, option_key);
	parse_scale(t, &config->continuous_scroll_max_y);
  xf86IDrvMsg(info, X_INFO, "%s %d\n", option_key, config->continuous_scroll_max_y);
}


void close_ir(struct ir *ir) {
  if (ir->timer != NULL) {
    TimerCancel(ir->timer);
    ir->timer = NULL;
  }
}
