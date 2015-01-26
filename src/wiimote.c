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

#include "wiimote.h"


BOOL wiimote_ir_is_active(struct wiimote *wiimote,
                          struct wiimote_config *config,
                          struct xwii_event *ev)
{
  return ir_is_active(&wiimote->ir, &config->ir, ev);
}

unsigned int xwii_key_to_wiimote_key(unsigned int keycode,
                                            InputInfoPtr info)
{
  switch(keycode) {
    case XWII_KEY_LEFT:
      return WIIMOTE_KEY_LEFT;
    case XWII_KEY_RIGHT:
      return WIIMOTE_KEY_RIGHT;
    case XWII_KEY_UP:
      return WIIMOTE_KEY_UP;
    case XWII_KEY_DOWN:
      return WIIMOTE_KEY_DOWN;
    case XWII_KEY_A:
      return WIIMOTE_KEY_A;
    case XWII_KEY_B:
      return WIIMOTE_KEY_B;
    case XWII_KEY_PLUS:
      return WIIMOTE_KEY_PLUS;
    case XWII_KEY_MINUS:
      return WIIMOTE_KEY_MINUS;
    case XWII_KEY_HOME:
      return WIIMOTE_KEY_HOME;
    case XWII_KEY_ONE:
      return WIIMOTE_KEY_ONE;
    case XWII_KEY_TWO:
      return WIIMOTE_KEY_TWO;
    default:
      xf86IDrvMsg(info, X_ERROR, "Invalid wiimote button %d\n", keycode);
      return 0;
  } 
}

static void configure_wiimote_keys(struct wiimote_config *config,
                                   char const * prefix,
                                   InputInfoPtr info) {
  char option_key[100];

  snprintf(option_key, sizeof(option_key), "%sLeft", prefix);
	configure_key(&config->keys[WIIMOTE_KEY_LEFT], option_key, info);

  snprintf(option_key, sizeof(option_key), "%sRight", prefix);
	configure_key(&config->keys[WIIMOTE_KEY_RIGHT], option_key, info);

  snprintf(option_key, sizeof(option_key), "%sUp", prefix);
	configure_key(&config->keys[WIIMOTE_KEY_UP], option_key, info);

  snprintf(option_key, sizeof(option_key), "%sDown", prefix);
	configure_key(&config->keys[WIIMOTE_KEY_DOWN], option_key, info);

  snprintf(option_key, sizeof(option_key), "%sA", prefix);
	configure_key(&config->keys[WIIMOTE_KEY_A], option_key, info);

  snprintf(option_key, sizeof(option_key), "%sB", prefix);
	configure_key(&config->keys[WIIMOTE_KEY_B], option_key, info);

  snprintf(option_key, sizeof(option_key), "%sPlus", prefix);
	configure_key(&config->keys[WIIMOTE_KEY_PLUS], option_key, info);

  snprintf(option_key, sizeof(option_key), "%sMinus", prefix);
	configure_key(&config->keys[WIIMOTE_KEY_MINUS], option_key, info);

  snprintf(option_key, sizeof(option_key), "%sHome", prefix);
	configure_key(&config->keys[WIIMOTE_KEY_HOME], option_key, info);

  snprintf(option_key, sizeof(option_key), "%sOne", prefix);
	configure_key(&config->keys[WIIMOTE_KEY_ONE], option_key, info);

  snprintf(option_key, sizeof(option_key), "%sTwo", prefix);
	configure_key(&config->keys[WIIMOTE_KEY_TWO], option_key, info);
}

void configure_wiimote(struct wiimote_config *config,
                       char const * prefix,
                       struct wiimote_config *defaults,
                       InputInfoPtr info) {
	const char *motion;

  memset(config, 0, sizeof(struct wiimote_config));

  if (defaults) {
	  memcpy(config, defaults, sizeof(struct wiimote_config));
  }

	motion = xf86FindOptionValue(info->options, "MotionSource");
	if (!motion)
		motion = "";

	if (!strcasecmp(motion, "accelerometer")) {
		config->motion_source = WIIMOTE_MOTION_SOURCE_ACCELEROMETER;
	} else if (!strcasecmp(motion, "ir")) {
		config->motion_source = WIIMOTE_MOTION_SOURCE_IR;
	} else if (!strcasecmp(motion, "motionplus")) {
		config->motion_source = WIIMOTE_MOTION_SOURCE_MOTIONPLUS;
	}

  configure_ir(&config->ir, prefix, info);
  configure_accelerometer(&config->accelerometer, prefix, info);
  configure_motionplus(&config->motionplus, "MP", info);

  configure_wiimote_keys(config, prefix, info);
}

void handle_wiimote_key(struct wiimote *wiimote,
                        struct wiimote_config *config,
                        struct xwii_event *ev,
                        unsigned int state,
                        InputInfoPtr info)
{
  unsigned int keycode;

  keycode = xwii_key_to_wiimote_key(ev->v.key.code, info);

  handle_key(&wiimote->keys[keycode], &config->keys[keycode], state, info);
}

void handle_wiimote_ir(struct wiimote *wiimote,
                       struct wiimote_config *config,
                       struct xwii_event *ev,
                       unsigned int state,
                       InputInfoPtr info)
{
	if (config->motion_source != WIIMOTE_MOTION_SOURCE_IR) return;
  handle_ir(&wiimote->ir, &config->ir, ev, info);
  handle_continuous_scrolling (&wiimote->ir, &config->ir, ev, info); 
}

void handle_wiimote_motionplus(struct wiimote *wiimote,
                               struct wiimote_config *config,
                               struct xwii_event *ev,
                               unsigned int state,
                               InputInfoPtr info)
{
	if (config->motion_source != WIIMOTE_MOTION_SOURCE_MOTIONPLUS) return;
  handle_motionplus(&wiimote->motionplus, &config->motionplus, ev, info);
}

void handle_wiimote_accelerometer(struct wiimote *wiimote,
                                  struct wiimote_config *config,
                                  struct xwii_event *ev,
                                  unsigned int state,
                                  InputInfoPtr info)
{
	if (config->motion_source != WIIMOTE_MOTION_SOURCE_ACCELEROMETER) return;
  handle_accelerometer(&wiimote->accelerometer, &config->accelerometer, ev, info);
}


void preinit_wiimote(struct wiimote_config *config) {
  preinit_motionplus(&config->motionplus);
}

