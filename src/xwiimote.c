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

#include <errno.h>
#include <exevents.h>
#include <inttypes.h>
#include <libudev.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <xf86.h>
#include <xf86Module.h>
#include <xf86Xinput.h>
#include <xkbsrv.h>
#include <xkbstr.h>
#include <xorg-server.h>
#include <xorgVersion.h>
#include <xserver-properties.h>
#include <xwiimote.h>

static char xwiimote_name[] = "xwiimote";

enum func_type {
	FUNC_IGNORE,
	FUNC_BTN,
	FUNC_KEY,
};

struct func {
	int type;
	union {
		int btn;
		unsigned int key;
	};
};

enum motion_type {
	MOTION_NONE,
	MOTION_ABS,
	MOTION_REL,
};

struct xwiimote_dev {
	InputInfoPtr info;
	void *handler;
	int dev_id;
	char *root;
	char *device;
	bool dup;
	struct xwii_iface *iface;

	XkbRMLVOSet rmlvo;
	unsigned int motion;
	struct func map_key[XWII_KEY_NUM];
};

/* List of all devices we know about to avoid duplicates */
static struct xwiimote_dev *xwiimote_devices[MAXDEVICES] = { NULL };

static BOOL xwiimote_is_dev(struct xwiimote_dev *dev)
{
	struct xwiimote_dev **iter = xwiimote_devices;

	if (dev->dev_id >= 0) {
		while (*iter) {
			if (dev != *iter && (*iter)->dev_id == dev->dev_id)
				return TRUE;
			iter++;
		}
	}

	return FALSE;
}

static void xwiimote_add_dev(struct xwiimote_dev *dev)
{
	struct xwiimote_dev **iter = xwiimote_devices;

	while (*iter)
		iter++;

	*iter = dev;
}

static void xwiimote_rm_dev(struct xwiimote_dev *dev)
{
	unsigned int num = 0;
	struct xwiimote_dev **iter = xwiimote_devices;

	while (*iter) {
		++num;
		if (*iter == dev) {
			memmove(iter, iter + 1,
					sizeof(xwiimote_devices) -
					(num * sizeof(*dev)));
			break;
		}
		iter++;
	}
}

static void cp_opt(struct xwiimote_dev *dev, const char *name, char **out)
{
	char *s;

	s = xf86SetStrOption(dev->info->options, name, NULL);
	if (!s || !s[0]) {
		free(s);
		*out = NULL;
	} else {
		*out = s;
	}
}

static int xwiimote_prepare_key(struct xwiimote_dev *dev, DeviceIntPtr device)
{
	cp_opt(dev, "XkbRules", &dev->rmlvo.rules);
	if (!dev->rmlvo.rules) {
		dev->rmlvo.rules = strdup("evdev");
		if (!dev->rmlvo.rules)
			return BadAlloc;
	}

	cp_opt(dev, "XkbModel", &dev->rmlvo.model);
	cp_opt(dev, "XkbLayout", &dev->rmlvo.layout);
	cp_opt(dev, "XkbVariant", &dev->rmlvo.variant);
	cp_opt(dev, "XkbOptions", &dev->rmlvo.options);

	if (!InitKeyboardDeviceStruct(device, &dev->rmlvo, NULL, NULL))
		return BadValue;

	return Success;
}

static int xwiimote_prepare_btn(struct xwiimote_dev *dev, DeviceIntPtr device)
{
	Atom *atoms;
	int num, ret = Success;
	char btn1[] = BTN_LABEL_PROP_BTN_LEFT;
	char btn2[] = BTN_LABEL_PROP_BTN_RIGHT;
	char btn3[] = BTN_LABEL_PROP_BTN_MIDDLE;
	unsigned char map[] = { 0 };

	num = 3;
	atoms = malloc(sizeof(*atoms) * num);
	if (!atoms)
		return BadAlloc;

	memset(atoms, 0, sizeof(*atoms) * num);
	atoms[0] = XIGetKnownProperty(btn1);
	atoms[1] = XIGetKnownProperty(btn2);
	atoms[2] = XIGetKnownProperty(btn3);

	if (!InitButtonClassDeviceStruct(device, 1, atoms, map)) {
		xf86IDrvMsg(dev->info, X_ERROR, "Cannot init button class\n");
		ret = BadValue;
		goto err_out;
	}

err_out:
	free(atoms);
	return ret;
}

static int xwiimote_prepare_abs(struct xwiimote_dev *dev, DeviceIntPtr device)
{
	Atom *atoms;
	int i, num, ret = Success;
	char absx[] = AXIS_LABEL_PROP_ABS_X;
	char absy[] = AXIS_LABEL_PROP_ABS_Y;

	num = 2;
	atoms = malloc(sizeof(*atoms) * num);
	if (!atoms)
		return BadAlloc;

	memset(atoms, 0, sizeof(*atoms) * num);
	atoms[0] = XIGetKnownProperty(absx);
	atoms[1] = XIGetKnownProperty(absy);

	if (!InitValuatorClassDeviceStruct(device, num, atoms,
					GetMotionHistorySize(), Absolute)) {
		xf86IDrvMsg(dev->info, X_ERROR, "Cannot init valuators\n");
		ret = BadValue;
		goto err_out;
	}

	for (i = 0; i < num; ++i) {
		xf86InitValuatorAxisStruct(device, i, atoms[i],
						-100, 100, 0, 0, 0, Absolute);
		xf86InitValuatorDefaults(device, i);
	}

err_out:
	free(atoms);
	return ret;
}

static int xwiimote_init(struct xwiimote_dev *dev, DeviceIntPtr device)
{
	int ret;

	ret = xwii_iface_new(&dev->iface, dev->root);
	if (ret) {
		xf86IDrvMsg(dev->info, X_ERROR, "Cannot alloc interface\n");
		return BadValue;
	}

	ret = xwiimote_prepare_key(dev, device);
	if (ret != Success) {
		xwii_iface_unref(dev->iface);
		return ret;
	}

	ret = xwiimote_prepare_btn(dev, device);
	if (ret != Success) {
		xwii_iface_unref(dev->iface);
		return ret;
	}

	ret = xwiimote_prepare_abs(dev, device);
	if (ret != Success) {
		xwii_iface_unref(dev->iface);
		return ret;
	}

	return Success;
}

static int xwiimote_close(struct xwiimote_dev *dev, DeviceIntPtr device)
{
	xwii_iface_unref(dev->iface);
	return Success;
}

static void xwiimote_key(struct xwiimote_dev *dev, struct xwii_event *ev)
{
	unsigned int code;
	unsigned int state;
	unsigned int key;
	int btn;
	int absolute = 0;

	code = ev->v.key.code;
	state = ev->v.key.state;
	if (code >= XWII_KEY_NUM)
		return;
	if (state > 1)
		return;

	if (dev->motion == MOTION_ABS)
		absolute = 1;

	switch (dev->map_key[code].type) {
		case FUNC_BTN:
			btn = dev->map_key[code].btn;
			xf86PostButtonEvent(dev->info->dev, absolute, btn,
								state, 0, 0);
			break;
		case FUNC_KEY:
			key = dev->map_key[code].key;
			xf86PostKeyboardEvent(dev->info->dev, key, state);
			break;
		case FUNC_IGNORE:
			/* fallthrough */
		default:
			break;
	}
}

static void xwiimote_accel(struct xwiimote_dev *dev, struct xwii_event *ev)
{
	int32_t x, y;

	if (dev->motion == MOTION_ABS) {
		x = ev->v.abs[0].x;
		y = -1 * ev->v.abs[0].y;
		xf86PostMotionEvent(dev->info->dev, Absolute, 0, 2, x, y);
	}
}

static void xwiimote_input(int fd, pointer data)
{
	struct xwiimote_dev *dev = data;
	InputInfoPtr info = dev->info;
	struct xwii_event ev;
	int ret;

	dev = info->private;
	if (dev->dup)
		return;

	do {
		memset(&ev, 0, sizeof(ev));
		ret = xwii_iface_poll(dev->iface, &ev);
		if (ret)
			break;

		switch (ev.type) {
			case XWII_EVENT_KEY:
				xwiimote_key(dev, &ev);
				break;
			case XWII_EVENT_ACCEL:
				xwiimote_accel(dev, &ev);
				break;
		}
	} while (!ret);

	if (ret != -EAGAIN) {
		xf86IDrvMsg(info, X_INFO, "Device disconnected\n");
		xf86RemoveInputHandler(dev->handler);
		xwii_iface_close(dev->iface, XWII_IFACE_ALL);
		info->fd = -1;
	}
}

static int xwiimote_on(struct xwiimote_dev *dev, DeviceIntPtr device)
{
	int ret;
	InputInfoPtr info = device->public.devicePrivate;

	ret = xwii_iface_open(dev->iface, XWII_IFACE_CORE | XWII_IFACE_ACCEL);
	if (ret) {
		xf86IDrvMsg(dev->info, X_ERROR, "Cannot open interface\n");
		return BadValue;
	}

	info->fd = xwii_iface_get_fd(dev->iface);
	if (info->fd >= 0) {
		dev->handler = xf86AddInputHandler(info->fd, xwiimote_input, dev);
	} else {
		xf86IDrvMsg(dev->info, X_ERROR, "Cannot get interface\n");
	}

	device->public.on = TRUE;

	return Success;
}

static int xwiimote_off(struct xwiimote_dev *dev, DeviceIntPtr device)
{
	InputInfoPtr info = device->public.devicePrivate;

	device->public.on = FALSE;

	if (info->fd >= 0) {
		xf86RemoveInputHandler(dev->handler);
		xwii_iface_close(dev->iface, XWII_IFACE_ALL);
		info->fd = -1;
	}

	return Success;
}

static int xwiimote_control(DeviceIntPtr device, int what)
{
	struct xwiimote_dev *dev;
	InputInfoPtr info;

	info = device->public.devicePrivate;
	dev = info->private;
	if (dev->dup)
		return Success;

	switch (what) {
		case DEVICE_INIT:
			return xwiimote_init(dev, device);
		case DEVICE_ON:
			return xwiimote_on(dev, device);
		case DEVICE_OFF:
			return xwiimote_off(dev, device);
		case DEVICE_CLOSE:
			return xwiimote_close(dev, device);
		default:
			return BadValue;
	}
}

/*
 * Check whether the device is actually a Wii Remote device and then retrieve
 * the sys-root of the HID device with the device-id.
 * Return TRUE if the device is a valid Wii Remote device.
 */
static BOOL xwiimote_validate(struct xwiimote_dev *dev)
{
	struct udev *udev;
	struct udev_device *d, *p;
	struct stat st;
	BOOL ret = TRUE;
	const char *root, *snum, *hid;
	int num;

	udev = udev_new();
	if (!udev) {
		xf86IDrvMsg(dev->info, X_ERROR, "Cannot create udev device\n");
		return FALSE;
	}

	if (stat(dev->device, &st)) {
		xf86IDrvMsg(dev->info, X_ERROR, "Cannot get device info\n");
		ret = FALSE;
		goto err_udev;
	}

	d = udev_device_new_from_devnum(udev, 'c', st.st_rdev);
	if (!d) {
		xf86IDrvMsg(dev->info, X_ERROR, "Cannot get udev device\n");
		ret = FALSE;
		goto err_udev;
	}

	p = udev_device_get_parent_with_subsystem_devtype(d, "hid", NULL);
	if (!p) {
		xf86IDrvMsg(dev->info, X_ERROR, "No HID device\n");
		ret = FALSE;
		goto err_dev;
	}

	hid = udev_device_get_property_value(p, "HID_ID");
	if (!hid || strcmp(hid, "0005:0000057E:00000306")) {
		xf86IDrvMsg(dev->info, X_ERROR, "No Wii Remote HID device\n");
		ret = FALSE;
		goto err_dev;
	}

	root = udev_device_get_syspath(p);
	snum = udev_device_get_sysname(p);
	snum = snum ? strchr(snum, '.') : NULL;
	if (!root || !snum) {
		xf86IDrvMsg(dev->info, X_ERROR, "Cannot get udev paths\n");
		ret = FALSE;
		goto err_dev;
	}

	num = strtol(&snum[1], NULL, 16);
	if (num < 0) {
		xf86IDrvMsg(dev->info, X_ERROR, "Invalid device id\n");
		ret = FALSE;
		goto err_dev;
	}

	dev->root = strdup(root);
	if (!dev->root) {
		xf86IDrvMsg(dev->info, X_ERROR, "Cannot allocate memory\n");
		ret = FALSE;
		goto err_dev;
	}

	dev->dev_id = num;

err_dev:
	udev_device_unref(d);
err_udev:
	udev_unref(udev);
	return ret;
}

static int xwiimote_preinit(InputDriverPtr drv, InputInfoPtr info, int flags)
{
	struct xwiimote_dev *dev;
	int ret;

	dev = malloc(sizeof(*dev));
	if (!dev)
		return BadAlloc;

	memset(dev, 0, sizeof(*dev));
	dev->info = info;
	dev->dev_id = -1;
	info->private = dev;
	info->type_name = (char*)XI_MOUSE;
	info->device_control = xwiimote_control;
	info->read_input = NULL;
	info->switch_mode = NULL;
	info->fd = -1;

	dev->device = xf86FindOptionValue(info->options, "Device");
	if (!dev->device) {
		xf86IDrvMsg(info, X_ERROR, "No Device specified\n");
		ret = BadMatch;
		goto err_free;
	}

	if (!xwiimote_validate(dev)) {
		ret = BadMatch;
		goto err_free;
	}

	/* Check for duplicate */
	if (!dev->info->name || strcmp(dev->info->name, XWII_NAME_CORE) ||
							xwiimote_is_dev(dev)) {
		xf86IDrvMsg(dev->info, X_INFO, "No core device\n");
		dev->dup = true;
		return Success;
	}
	xf86IDrvMsg(dev->info, X_INFO, "Is a core device\n");

	xwiimote_add_dev(dev);

	return Success;

err_free:
	free(dev);
	info->private = NULL;
	return ret;
}

static void xwiimote_uninit(InputDriverPtr drv, InputInfoPtr info, int flags)
{
	struct xwiimote_dev *dev;

	if (!info)
		return;

	if (info->private) {
		dev = info->private;
		if (!dev->dup) {
			XkbFreeRMLVOSet(&dev->rmlvo, FALSE);
			xwiimote_rm_dev(dev);
		}
		free(dev->root);
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
