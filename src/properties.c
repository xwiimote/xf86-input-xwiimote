/*
 * Copyright 2007-2008 by Sascha Hlusiak. <saschahlusiak@freedesktop.org>     
 *                                                                            
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is  hereby granted without fee, provided that
 * the  above copyright   notice appear  in   all  copies and  that both  that
 * copyright  notice   and   this  permission   notice  appear  in  supporting
 * documentation, and that   the  name of  Sascha   Hlusiak  not  be  used  in
 * advertising or publicity pertaining to distribution of the software without
 * specific,  written      prior  permission.     Sascha   Hlusiak   makes  no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.                   
 *                                                                            
 * SASCHA  HLUSIAK  DISCLAIMS ALL   WARRANTIES WITH REGARD  TO  THIS SOFTWARE,
 * INCLUDING ALL IMPLIED   WARRANTIES OF MERCHANTABILITY  AND   FITNESS, IN NO
 * EVENT  SHALL SASCHA  HLUSIAK  BE   LIABLE   FOR ANY  SPECIAL, INDIRECT   OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA  OR PROFITS, WHETHER  IN  AN ACTION OF  CONTRACT,  NEGLIGENCE OR OTHER
 * TORTIOUS  ACTION, ARISING    OUT OF OR   IN  CONNECTION  WITH THE USE    OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
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

#define PROP_NAME_MODE "mode"
static Atom prop_mode = 0;

static int
xwiimote_set_property(DeviceIntPtr device, Atom atom, XIPropertyValuePtr val, BOOL checkonly)
{
  InputInfoPtr info = device->public.devicePrivate;
  struct xwiimote_dev *dev = info->private;
  int mode = -1;

  if (atom == prop_mode) {
      if (val->size != 1 || val->format != 8 || val->type != XA_INTEGER)
      {
          return BadMatch;
      }
      switch(*((INT8*)val->data)) {
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
  XIChangeDeviceProperty(device, prop_mode, XA_INTEGER, 8,
                              PropModeReplace, 1,
                              &dev->wiimote.ir.mode,
                              FALSE);
  XISetDevicePropertyDeletable(device, prop_mode, FALSE);
  return TRUE;
}
