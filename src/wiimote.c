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


void configure_wiimote(struct wiimote_config *config,
                       InputInfoPtr info) {
}

void handle_wiimote_key(struct wiimote *wiimote,
                        struct wiimote_config *config,
                        struct xwii_event *ev,
                        unsigned int state,
                        InputInfoPtr info)
{
  unsigned int keycode;
  struct key *key;
  struct key_config *key_config;

  keycode = ev->v.key.code;
  key = &wiimote->keys[keycode];
  key_config = &config->keys[keycode];

  handle_key(key, key_config, state, info);
}

void handle_wiimote_ir(struct wiimote *wiimote,
                       struct wiimote_config *config,
                       struct xwii_event *ev,
                       unsigned int state,
                       InputInfoPtr info)
{
  handle_ir(&wiimote->ir, &config->ir, ev, info);
}

void handle_wiimote_motionplus(struct wiimote *wiimote,
                               struct wiimote_config *config,
                               struct xwii_event *ev,
                               unsigned int state,
                               InputInfoPtr info)
{
  handle_motionplus(&wiimote->motionplus, &config->motionplus, ev, info);
}

void handle_wiimote_accelerometer(struct wiimote *wiimote,
                                  struct wiimote_config *config,
                                  struct xwii_event *ev,
                                  unsigned int state,
                                  InputInfoPtr info)
{
  handle_accelerometer(&wiimote->accelerometer, &config->accelerometer, ev, info);
}


/* TODO
void refresh_wiimote(struct xwiimote_dev *dev)
{
	int ret;

	ret = xwii_iface_open(dev->iface, dev->ifs);
	if (ret)
		xf86IDrvMsg(dev->info, X_INFO, "Cannot open all requested interfaces\n");
}
*/


void preinit_wiimote(struct wiimote_config *config) {
  preinit_motionplus(&config->motionplus);
  preinit_ir(&config->ir);
}
