/*
 * XWiimote
 *
 * Copyright (c) 2011, 2012 David Herrmann <dh.herrmann@googlemail.com>
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

#include <stdlib.h>
#include <xf86.h>
#include <xf86Module.h>
#include <xf86Xinput.h>
#include <xorg-server.h>
#include <xorgVersion.h>

static char xwiimote_name[] = "xwiimote";

struct xwiimote_dev {
	char *device;
};

static int xwiimote_control(DeviceIntPtr device, int what)
{
	return Success;
}

static void xwiimote_input(InputInfoPtr info)
{
}

static int xwiimote_preinit(InputDriverPtr drv, InputInfoPtr info, int flags)
{
	struct xwiimote_dev *dev;
	int ret;

	dev = malloc(sizeof(*dev));
	if (!dev)
		return BadAlloc;

	memset(dev, 0, sizeof(*dev));
	info->private = dev;
	info->device_control = xwiimote_control;
	info->read_input = xwiimote_input;
	info->switch_mode = NULL;

	dev->device = xf86FindOptionValue(info->options, "Device");
	if (!dev->device) {
		xf86IDrvMsg(info, X_ERROR, "No Device specified\n");
		ret = BadMatch;
		goto err_free;
	}

	return Success;

err_free:
	free(dev);
	return ret;
}

static void xwiimote_uninit(InputDriverPtr drv, InputInfoPtr info, int flags)
{
	struct xwiimote_dev *dev;

	if (!info)
		return;

	if (info->private) {
		dev = info->private;
		free(dev);
		info->private = NULL;
	}

	xf86DeleteInput(info, flags);
}

_X_EXPORT InputDriverRec xwiimote_driver = {
	1,
	xwiimote_name,
	NULL,
	xwiimote_preinit,
	xwiimote_uninit,
	NULL,
	0,
};

static pointer xwiimote_plug(pointer module,
				pointer options,
				int *errmaj,
				int *errmin)
{
	xf86AddInputDriver(&xwiimote_driver, module, 0);
	return module;
}

static void xwiimote_unplug(pointer p)
{
}

static XF86ModuleVersionInfo xwiimote_version =
{
	xwiimote_name,
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR,
	PACKAGE_VERSION_PATCHLEVEL,
	ABI_CLASS_XINPUT,
	ABI_XINPUT_VERSION,
	MOD_CLASS_XINPUT,
	{0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData xwiimoteModuleData =
{
	&xwiimote_version,
	&xwiimote_plug,
	&xwiimote_unplug,
};




#if 0
#include <linux/input.h>
#include <linux/types.h>

#include <xf86_OSproc.h>
#include <xkbsrv.h>

#include <X11/keysym.h>
#include <X11/extensions/XI.h>
#include <exevents.h>

#include <X11/Xatom.h>
#include <xserver-properties.h>

#include <unistd.h>

#include <stdio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>

#define SYSCALL(call) while (((call) == -1) && (errno == EINTR))

static InputInfoPtr RandomPreInit(InputDriverPtr  drv, IDevPtr dev, int flags);
static void RandomUnInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags);
static pointer RandomPlug(pointer module, pointer options, int *errmaj, int  *errmin);
static void RandomUnplug(pointer p);
static void RandomReadInput(InputInfoPtr pInfo);
static int RandomControl(DeviceIntPtr    device,int what);
static int _random_init_buttons(DeviceIntPtr device);
static int _random_init_axes(DeviceIntPtr device);


static int
_random_init_buttons(DeviceIntPtr device)
{
    InputInfoPtr        pInfo = device->public.devicePrivate;
    RandomDevicePtr     pRandom = pInfo->private;
    CARD8               *map;
    int                 i;
    int                 ret = Success;
    const int           num_buttons = 2;

    map = xcalloc(num_buttons, sizeof(CARD8));

    for (i = 0; i < num_buttons; i++)
        map[i] = i;

    pRandom->labels = xalloc(sizeof(Atom));

    if (!InitButtonClassDeviceStruct(device, num_buttons, pRandom->labels, map)) {
            xf86Msg(X_ERROR, "%s: Failed to register buttons.\n", pInfo->name);
            ret = BadAlloc;
    }

    xfree(map);
    return ret;
}

static void RandomInitAxesLabels(RandomDevicePtr pRandom, int natoms, Atom *atoms)
{
#ifdef HAVE_LABELS
    Atom atom;
    int axis;
    char **labels;
    int labels_len = 0;
    char *misc_label;

    labels     = rel_labels;
    labels_len = ArrayLength(rel_labels);
    misc_label = AXIS_LABEL_PROP_REL_MISC;

    memset(atoms, 0, natoms * sizeof(Atom));

    /* Now fill the ones we know */
    for (axis = 0; axis < labels_len; axis++)
    {
        if (pRandom->axis_map[axis] == -1)
            continue;

        atom = XIGetKnownProperty(labels[axis]);
        if (!atom) /* Should not happen */
            continue;

        atoms[pRandom->axis_map[axis]] = atom;
    }
#endif
}


static int
_random_init_axes(DeviceIntPtr device)
{
    InputInfoPtr        pInfo = device->public.devicePrivate;
    RandomDevicePtr     pRandom = pInfo->private;
    int                 i;
    const int           num_axes = 2;
    Atom                * atoms;

    pRandom->num_vals = num_axes;
    atoms = xalloc(pRandom->num_vals * sizeof(Atom));

    RandomInitAxesLabels(pRandom, pRandom->num_vals, atoms);
    if (!InitValuatorClassDeviceStruct(device,
                num_axes,
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 7
                atoms,
#endif	
                GetMotionHistorySize(),
                0))
        return BadAlloc;

    pInfo->dev->valuator->mode = Relative;
    if (!InitAbsoluteClassDeviceStruct(device))
            return BadAlloc;

    for (i = 0; i < pRandom->axes; i++) {
            xf86InitValuatorAxisStruct(device, i, *pRandom->labels, -1, -1, 1, 1, 1);
            xf86InitValuatorDefaults(device, i);
    }
    xfree(atoms);
    return Success;
}

static int RandomControl(DeviceIntPtr    device,
                         int             what)
{
    InputInfoPtr  pInfo = device->public.devicePrivate;
    RandomDevicePtr pRandom = pInfo->private;

    switch(what)
    {
        case DEVICE_INIT:
            _random_init_buttons(device);
            _random_init_axes(device);
            break;

        /* Switch device on.  Establish socket, start event delivery.  */
        case DEVICE_ON:
            xf86Msg(X_INFO, "%s: On.\n", pInfo->name);
            if (device->public.on)
                    break;

            SYSCALL(pInfo->fd = open(pRandom->device, O_RDONLY | O_NONBLOCK));
            if (pInfo->fd < 0)
            {
                xf86Msg(X_ERROR, "%s: cannot open device.\n", pInfo->name);
                return BadRequest;
            }

            xf86FlushInput(pInfo->fd);
            xf86AddEnabledDevice(pInfo);
            device->public.on = TRUE;
            break;
       case DEVICE_OFF:
            xf86Msg(X_INFO, "%s: Off.\n", pInfo->name);
            if (!device->public.on)
                break;
            xf86RemoveEnabledDevice(pInfo);
            close(pInfo->fd);
            pInfo->fd = -1;
            device->public.on = FALSE;
            break;
      case DEVICE_CLOSE:
            /* free what we have to free */
            break;
    }
    return Success;
}

static void RandomReadInput(InputInfoPtr pInfo)
{
    char data;

    while(xf86WaitForInput(pInfo->fd, 0) > 0)
    {
        read(pInfo->fd, &data, 1);

        xf86PostMotionEvent(pInfo->dev,
                            0, /* is_absolute */
                            0, /* first_valuator */
                            1, /* num_valuators */
                            data);
    }
}

#endif
