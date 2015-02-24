/*
 * XWiimote
 *
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
 * Copyright (c) 2015 Zachary Dovel <zakkudo2@gmail.com>
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
#include <xf86Module.h>

#include <misc.h>
#include <xf86.h>
#include <X11/Xatom.h>
#include <xf86Xinput.h>
#include <exevents.h>

#include "properties.h"

#define PROP_NAME_MODE "Wii Remote Mode"
static Atom prop_mode = 0;

static int
xwiimote_set_property(DeviceIntPtr device, Atom atom, XIPropertyValuePtr val, BOOL checkonly)
{
  InputInfoPtr info = device->public.devicePrivate;
  struct xwiimote_dev *dev = info->private;
  int mode = -1;

  if (atom == prop_mode) {
      if (val->size != 1 || val->format != 32 || val->type != XA_INTEGER)
      {
          return BadMatch;
      }
      switch(*((INT32*)val->data)) {
        case IR_MODE_GAME:
          mode = IR_MODE_GAME;
          break;
        case IR_MODE_MENU:
          mode = IR_MODE_MENU;
          break;
        default:
          return BadMatch;
      }
      if (!checkonly) dev->wiimote.ir.mode = mode;
  }

  /* property not handled, report success */
  return Success;
}

BOOL
xwiimote_initialize_properties(DeviceIntPtr device, struct xwiimote_dev *dev)
{
  XIRegisterPropertyHandler(device, xwiimote_set_property, NULL, NULL);

  /* Debug Level */
  prop_mode = MakeAtom(PROP_NAME_MODE, strlen(PROP_NAME_MODE), TRUE);
  XIChangeDeviceProperty(device, prop_mode, XA_INTEGER, 32,
                              PropModeReplace, 1,
                              &dev->wiimote.ir.mode,
                              TRUE);
  XISetDevicePropertyDeletable(device, prop_mode, FALSE);
  return TRUE;
}
