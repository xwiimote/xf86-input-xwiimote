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

#include "ir.h"


BOOL ir_is_active(struct ir *ir,
                  struct ir_config *config,
                  struct xwii_event *ev)
{
	return (ev->time.tv_sec < ir->last_valid_event.tv_sec + config->keymap_expiry_secs
			|| (ev->time.tv_sec == ir->last_valid_event.tv_sec + config->keymap_expiry_secs
				&& ev->time.tv_usec < ir->last_valid_event.tv_usec));
}

void handle_ir(struct ir *ir,
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
				dists[0] = XWIIMOTE_DISTSQ(c->x, c->y, ir->ref_x, ir->ref_y);
				dists[1] = XWIIMOTE_DISTSQ(c->x, c->y, d.x, d.y);
				dists[2] = XWIIMOTE_DISTSQ(a->x, a->y, ir->ref_x, ir->ref_y);
				dists[3] = XWIIMOTE_DISTSQ(a->x, a->y, d.x, d.y);
				dists[4] = XWIIMOTE_DISTSQ(b->x, b->y, ir->ref_x, ir->ref_y);
				dists[5] = XWIIMOTE_DISTSQ(b->x, b->y, d.x, d.y);
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
		if (XWIIMOTE_DISTSQ(a->x, a->y, ir->ref_x, ir->ref_y)
				< XWIIMOTE_DISTSQ(b->x, b->y, ir->ref_x, ir->ref_y)) {
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
	if (XWIIMOTE_DISTSQ(a->x, a->y, ir->avg_x, ir->avg_y)
			< config->avg_radius * config->avg_radius) {
		if (ir->avg_count >= config->avg_min_samples) {
			a->x = (a->x + ir->avg_x * config->avg_weight) / (config->avg_weight+1);
			a->y = (a->y + ir->avg_y * config->avg_weight) / (config->avg_weight+1);
		}
	} else {
		ir->avg_count = 0;
	}

  /* Absolute scrolling */
  {
    int abs_x, abs_y;

    /* Calculate the absolute x value */
    abs_x = IR_MAX_X - a->x;
    if (abs_x < config->continuous_scroll_border) {
      abs_x = config->continuous_scroll_border;
    } else if (abs_x > IR_MAX_X - config->continuous_scroll_border) {
      abs_x = IR_MAX_X - config->continuous_scroll_border;
    }

    /* Calculate the absolute y value */
    abs_y = a->y;
    if (abs_y < config->continuous_scroll_border) {
      abs_y = config->continuous_scroll_border;
    } else if (abs_y > IR_MAX_Y - config->continuous_scroll_border) {
      abs_y = IR_MAX_Y - config->continuous_scroll_border;
    }

    /* Moves cursor smoothly to the point pointed at with a transition */
    {
      double actual_distance;
      double angle;

      actual_distance = abs(sqrt(pow(abs_x, 2) + pow(abs_y, 2)));

      if (actual_distance < IR_SMOOTH_SCROLL_DISTANCE) {
          ir->smooth_scroll_x = abs_x;
          ir->smooth_scroll_y = abs_y;
      }
      else {
        if (abs_x != 0 && abs_y != 0) {
          angle = atan(abs_y/abs_x);
          ir->smooth_scroll_x += sin(angle) * IR_SMOOTH_SCROLL_DISTANCE;
          ir->smooth_scroll_y += cos(angle) * IR_SMOOTH_SCROLL_DISTANCE;
        } else if (abs_x != 0) {
          ir->smooth_scroll_x += IR_SMOOTH_SCROLL_DISTANCE;
          ir->smooth_scroll_y = 0;
        } else if (abs_y != 0) {
          ir->smooth_scroll_x = 0;
          ir->smooth_scroll_y += IR_SMOOTH_SCROLL_DISTANCE;
        } else {
          ir->smooth_scroll_x = 0;
          ir->smooth_scroll_y = 0;
        }
      }
    }

    xf86PostMotionEvent(info->dev, 1, 0, 2, (int) ir->smooth_scroll_x, (int) ir->smooth_scroll_y);
  }

  /* Continuous scrolling at the edges of the screen */
  {
    int scroll_x, scroll_y;
    double x_scale, y_scale;

    x_scale = config->continuous_scroll_max_x / config->continuous_scroll_border;
    if (a->x < config->continuous_scroll_border) {
      ir->continuous_scroll_subpixel_x += (-a->x) * x_scale;
    } else if (a->x > IR_MAX_X - config->continuous_scroll_border) {
      ir->continuous_scroll_subpixel_x += (a->x - (IR_MAX_X - config->continuous_scroll_border)) * x_scale;
    } else {
      scroll_x = 0;
      ir->continuous_scroll_subpixel_x = 0;
    }
    scroll_x = (int) ir->continuous_scroll_subpixel_x;
    ir->continuous_scroll_subpixel_x -= scroll_x; 

    y_scale = config->continuous_scroll_max_y / config->continuous_scroll_border;
    if (a->y < config->continuous_scroll_border) {
      ir->continuous_scroll_subpixel_y += (-a->y) * y_scale;
    } else if (a->y > IR_MAX_Y - config->continuous_scroll_border) {
      ir->continuous_scroll_subpixel_y += (a->y - (IR_MAX_Y - config->continuous_scroll_border)) * y_scale;
    } else {
      scroll_y = 0;
      ir->continuous_scroll_subpixel_y = 0;
    }
    scroll_y = (int) ir->continuous_scroll_subpixel_y;
    ir->continuous_scroll_subpixel_y -= scroll_y; 

    xf86PostMotionEvent(info->dev, 0, 0, 2, scroll_x, scroll_y);
  }

	ir->last_valid_event = ev->time;
}


static void parse_scale(const char *t, int *out)
{
	if (!t)
		return;

	*out = atoi(t);
}


void configure_ir(struct ir_config *config, 
                  char const *name,
                  InputInfoPtr info)
{
	const char *t;

	t = xf86FindOptionValue(info->options, "IRAvgRadius");
	parse_scale(t, &config->avg_radius);

	t = xf86FindOptionValue(info->options, "IRAvgMaxSamples");
	parse_scale(t, &config->avg_max_samples);
	if (config->avg_max_samples < 1) config->avg_max_samples = 1;

	t = xf86FindOptionValue(info->options, "IRAvgMinSamples");
	parse_scale(t, &config->avg_min_samples);
	if (config->avg_min_samples < 1) {
		config->avg_min_samples = 1;
	} else if (config->avg_min_samples > config->avg_max_samples) {
		config->avg_min_samples = config->avg_max_samples;
	}

	t = xf86FindOptionValue(info->options, "IRAvgWeight");
	parse_scale(t, &config->avg_weight);
	if (config->avg_weight < 0) config->avg_weight = 0;

	t = xf86FindOptionValue(info->options, "IRKeymapExpirySecs");
	parse_scale(t, &config->keymap_expiry_secs);

	t = xf86FindOptionValue(info->options, "IRContinuousScrollBorder");
	parse_scale(t, &config->continuous_scroll_border);

	t = xf86FindOptionValue(info->options, "IRContinuousScrollMaxX");
	parse_scale(t, &config->continuous_scroll_max_x);

	t = xf86FindOptionValue(info->options, "IRContinuousScrollMaxY");
	parse_scale(t, &config->continuous_scroll_max_y);
}

void preinit_ir (struct ir_config *config) {
	config->avg_radius = IR_AVG_RADIUS;
	config->avg_max_samples = IR_AVG_MAX_SAMPLES;
	config->avg_min_samples = IR_AVG_MIN_SAMPLES;
	config->avg_weight = IR_AVG_WEIGHT;
	config->keymap_expiry_secs = IR_KEYMAP_EXPIRY_SECS;
	config->continuous_scroll_border = IR_CONTINUOUS_SCROLL_BORDER;
  config->continuous_scroll_max_x = IR_CONTINUOUS_SCROLL_MAX_X;
  config->continuous_scroll_max_y = IR_CONTINUOUS_SCROLL_MAX_Y;
}
