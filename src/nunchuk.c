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

#include "nunchuk.h"


void handle_nunchuk_analog_stick(struct nunchuk *nunchuk,
                                 struct nunchuk_config *config,
                                 struct xwii_event *ev,
                                 unsigned int state,
                                 InputInfoPtr info)
{
  handle_analog_stick(&nunchuk->analog_stick, &config->analog_stick, ev, 0, state, info);
}


void handle_nunchuk_key(struct nunchuk *nunchuk,
                        struct nunchuk_config *config,
                        struct xwii_event *ev,
                        unsigned int state,
                        InputInfoPtr info)
{  
  unsigned int keycode;

  keycode = ev->v.key.code;

  switch(keycode) {
    case XWII_KEY_C:
      keycode = NUNCHUK_KEY_C;
      break;
    case XWII_KEY_Z:
      keycode = NUNCHUK_KEY_Z;
      break;
    default:
      xf86IDrvMsg(info, X_ERROR, "Invalid nunchuk button %d\n", keycode);
      return;
  } 
  handle_key(&nunchuk->keys[keycode], &config->keys[keycode], state, info);
}

void configure_nunchuk(struct nunchuk_config *config,
                       char const * prefix,
                       struct nunchuk_config *defaults,
                       InputInfoPtr info)
{
  char option_key[100];
  char analog_stick_prefix[100];

	memset(config, 0, sizeof(struct nunchuk_config));

  if (defaults) {
	  memcpy(config, defaults, sizeof(struct nunchuk_config));
  }

  /* AnalogStick */
  snprintf(analog_stick_prefix, sizeof(analog_stick_prefix), "%sNunchuk", prefix);
	configure_analog_stick(&config->analog_stick, analog_stick_prefix, info);

  /* Keys */
  snprintf(option_key, sizeof(option_key), "%sC", prefix);
	configure_key(&config->keys[NUNCHUK_KEY_C], option_key, info);

  snprintf(option_key, sizeof(option_key), "%sZ", prefix);
	configure_key(&config->keys[NUNCHUK_KEY_Z], option_key, info);
}
