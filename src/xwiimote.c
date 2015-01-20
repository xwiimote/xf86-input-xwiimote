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
#include <errno.h>
#include <exevents.h>
#include <inttypes.h>
#include <libudev.h>
#include <linux/input.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <xf86.h>
#include <xf86Module.h>
#include <xf86Xinput.h>
#include <xkbsrv.h>
#include <xkbstr.h>
#include <xorgVersion.h>
#include <xserver-properties.h>
#include <xwiimote.h>

#include "nunchuk.h"

static char xwiimote_name[] = "xwiimote";



static struct func map_key_default[XWII_KEY_NUM] = {
	/* Wiimote */
	[XWII_KEY_LEFT] = { .type = FUNC_KEY, .u.key = KEY_LEFT },
	[XWII_KEY_RIGHT] = { .type = FUNC_KEY, .u.key = KEY_RIGHT },
	[XWII_KEY_UP] = { .type = FUNC_KEY, .u.key = KEY_UP },
	[XWII_KEY_DOWN] = { .type = FUNC_KEY, .u.key = KEY_DOWN },
	[XWII_KEY_A] = { .type = FUNC_KEY, .u.key = KEY_ENTER },
	[XWII_KEY_B] = { .type = FUNC_KEY, .u.key = KEY_SPACE },
	[XWII_KEY_PLUS] = { .type = FUNC_KEY, .u.key = KEY_VOLUMEUP },
	[XWII_KEY_MINUS] = { .type = FUNC_KEY, .u.key = KEY_VOLUMEDOWN },
	[XWII_KEY_HOME] = { .type = FUNC_KEY, .u.key = KEY_ESC },
	[XWII_KEY_ONE] = { .type = FUNC_KEY, .u.key = KEY_1 },
	[XWII_KEY_TWO] = { .type = FUNC_KEY, .u.key = KEY_2 },

	/* Nunchuck */
	[XWII_KEY_C] = { .type = FUNC_KEY, .u.key = KEY_LEFTCTRL },
	[XWII_KEY_Z] = { .type = FUNC_KEY, .u.key = KEY_LEFTSHIFT },

	/*Classic Controller*/
	[XWII_KEY_X] =  { .type = FUNC_KEY, .u.key = KEY_RIGHTSHIFT },
	[XWII_KEY_Y] =  { .type = FUNC_KEY, .u.key = KEY_RIGHTCTRL },
	[XWII_KEY_TL] = { .type = FUNC_KEY, .u.key = KEY_PAGEUP },
	[XWII_KEY_TR] = { .type = FUNC_KEY, .u.key = KEY_PAGEDOWN },
	[XWII_KEY_ZL] = { .type = FUNC_KEY, .u.key = KEY_HOME },
	[XWII_KEY_ZR] = { .type = FUNC_KEY, .u.key = KEY_END },

	/* Pro Controller*/
	[XWII_KEY_THUMBL] =  { .type = FUNC_KEY, .u.key = KEY_LEFTSHIFT },
	[XWII_KEY_THUMBR] =  { .type = FUNC_KEY, .u.key = KEY_LEFTSHIFT },
};

enum motion_type {
	MOTION_NONE,
	MOTION_ABS,
	MOTION_REL,
};


enum motion_source {
	SOURCE_NONE,
	SOURCE_ACCEL,
	SOURCE_IR,
	SOURCE_MOTIONPLUS,
};

enum keyset {
	KEYSET_NORMAL,
	KEYSET_IR,
	KEYSET_NUM
};

struct xwiimote_dev {
	InputInfoPtr info;
	void *handler;
	int dev_id;
	char *root;
	const char *device;
	bool dup;
	struct xwii_iface *iface;
	unsigned int ifs;

	XkbRMLVOSet rmlvo;
	unsigned int motion;
	unsigned int motion_source;
	enum keyset key_pressed[XWII_KEY_NUM];

  struct wiimote wiimote;
  struct wiimote_config wiimote_config[KEYSET_NUM];
  struct nunchuk nunchuk;
  struct nunchuk_config nunchuk_config[KEYSET_NUM];
};

/* List of all devices we know about to avoid duplicates */
static struct xwiimote_dev *xwiimote_devices[MAXDEVICES + 1];

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
			/* last device is always NULL so no need to clear it */
			memmove(iter, iter + 1,
					sizeof(xwiimote_devices) -
					(num * sizeof(*iter)));
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
	cp_opt(dev, "xkb_rules", &dev->rmlvo.rules);
	if (!dev->rmlvo.rules)
		cp_opt(dev, "XkbRules", &dev->rmlvo.rules);
	cp_opt(dev, "xkb_model", &dev->rmlvo.model);
	if (!dev->rmlvo.model)
		cp_opt(dev, "XkbModel", &dev->rmlvo.model);
	cp_opt(dev, "xkb_layout", &dev->rmlvo.layout);
	if (!dev->rmlvo.layout)
		cp_opt(dev, "XkbLayout", &dev->rmlvo.layout);
	cp_opt(dev, "xkb_variant", &dev->rmlvo.variant);
	if (!dev->rmlvo.variant)
		cp_opt(dev, "XkbVariant", &dev->rmlvo.variant);
	cp_opt(dev, "xkb_options", &dev->rmlvo.options);
	if (!dev->rmlvo.options)
		cp_opt(dev, "XkbOptions", &dev->rmlvo.options);

	if (!InitKeyboardDeviceStruct(device, &dev->rmlvo, NULL, NULL))
		return BadValue;

	return Success;
}

static int xwiimote_prepare_btn(struct xwiimote_dev *dev, DeviceIntPtr device)
{
	Atom *atoms;
	int num, ret = Success;
	char btn0[] = BTN_LABEL_PROP_BTN_UNKNOWN;
	char btn1[] = BTN_LABEL_PROP_BTN_LEFT;
	char btn2[] = BTN_LABEL_PROP_BTN_RIGHT;
	char btn3[] = BTN_LABEL_PROP_BTN_MIDDLE;
	unsigned char map[] = { 0, 1, 2, 3 };

	num = 4;
	atoms = malloc(sizeof(*atoms) * num);
	if (!atoms)
		return BadAlloc;

	memset(atoms, 0, sizeof(*atoms) * num);
	atoms[0] = XIGetKnownProperty(btn0);
	atoms[1] = XIGetKnownProperty(btn1);
	atoms[2] = XIGetKnownProperty(btn2);
	atoms[3] = XIGetKnownProperty(btn3);

	if (!InitButtonClassDeviceStruct(device, 1, atoms, map)) {
		xf86IDrvMsg(dev->info, X_ERROR, "Cannot init button class\n");
		ret = BadValue;
		goto err_out;
	}

err_out:
	free(atoms);
	return ret;
}

static int xwiimote_prepare_abs(struct xwiimote_dev *dev, DeviceIntPtr device, int xmin, int xmax, int ymin, int ymax)
{
	Atom *atoms;
	int num, ret = Success;
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

	xf86InitValuatorAxisStruct(device, 0, atoms[0], xmin, xmax, 0, 0, 0, Absolute);
	xf86InitValuatorDefaults(device, 0);
	xf86InitValuatorAxisStruct(device, 1, atoms[1], ymin, ymax, 0, 0, 0, Absolute);
	xf86InitValuatorDefaults(device, 1);

err_out:
	free(atoms);
	return ret;
}

static int xwiimote_prepare_rel(struct xwiimote_dev *dev, DeviceIntPtr device, int xmin, int xmax, int ymin, int ymax)
{
	Atom *atoms;
	int num, ret = Success;
	char relx[] = AXIS_LABEL_PROP_REL_X;
	char rely[] = AXIS_LABEL_PROP_REL_Y;

	num = 2;
	atoms = malloc(sizeof(*atoms) * num);
	if (!atoms)
		return BadAlloc;

	memset(atoms, 0, sizeof(*atoms) * num);
	atoms[0] = XIGetKnownProperty(relx);
	atoms[1] = XIGetKnownProperty(rely);

	if (!InitValuatorClassDeviceStruct(device, num, atoms,
					   GetMotionHistorySize(),
					   Relative)) {
		xf86IDrvMsg(dev->info, X_ERROR, "Cannot init valuators\n");
		ret = BadValue;
		goto err_out;
	}

	xf86InitValuatorAxisStruct(device, 0, atoms[0], xmin, xmax, 0, 0, 0, Relative);
	xf86InitValuatorDefaults(device, 0);
	xf86InitValuatorAxisStruct(device, 1, atoms[1], ymin, ymax, 0, 0, 0, Relative);
	xf86InitValuatorDefaults(device, 1);

err_out:
	free(atoms);
	return ret;
}

static int xwiimote_init(struct xwiimote_dev *dev, DeviceIntPtr device)
{
	int ret;

	ret = xwiimote_prepare_key(dev, device);
	if (ret != Success)
		return ret;

	ret = xwiimote_prepare_btn(dev, device);
	if (ret != Success)
		return ret;

	switch(dev->motion_source) {
    case SOURCE_ACCEL:
      ret = xwiimote_prepare_abs(dev, device, -100, 100, -100, 100);
      break;
    case SOURCE_MOTIONPLUS:
      ret = xwiimote_prepare_rel(dev, device, -10000, 10000, -10000, 10000);
      break;
    case SOURCE_IR:
      ret = xwiimote_prepare_abs(dev, device, 
        XWIIMOTE_IR_CONTINUOUS_SCROLL_BORDER, XWIIMOTE_IR_MAX_X - XWIIMOTE_IR_CONTINUOUS_SCROLL_BORDER,
        XWIIMOTE_IR_CONTINUOUS_SCROLL_BORDER, XWIIMOTE_IR_MAX_Y - XWIIMOTE_IR_CONTINUOUS_SCROLL_BORDER);
      break;
    default:
      ret = Success;
      break;
	}

	if (ret != Success)
		return ret;

	return Success;
}

static int xwiimote_close(struct xwiimote_dev *dev, DeviceIntPtr device)
{
	return Success;
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
		ret = xwii_iface_dispatch(dev->iface, &ev, sizeof(ev));
		if (ret)
			break;

    if (ir_is_active (ir_config, &ev)) {
      state =  KEYSET_IR;
    } else {
      state = 0;
    }

		switch (ev.type) {
			case XWII_EVENT_WATCH:
				refresh_wiimote(dev);
				refresh_nunchuk(dev);
				break;
			case XWII_EVENT_KEY:
				handle_wiimote_key(wiimote, wiimote_config, &ev, state, dev->info);
				break;
			case XWII_EVENT_ACCEL:
	      if (dev->motion_source == SOURCE_ACCEL) {
				  handle_wiimote_accelerometer(wiimote, wiimote_config, &ev, state, dev->info);
        }
				break;
			case XWII_EVENT_IR:
	      if (dev->motion_source == SOURCE_IR) {
				  handle_wiimote_ir(wiimote, wiimote_config, &ev, state, dev->info);
        }
			case XWII_EVENT_MOTION_PLUS:
	      if (dev->motion_source == SOURCE_MOTIONPLUS) {
				  handle_wiimote_motionplus(wiimote, wiimote_config, &ev, state, dev->info);
        }
				break;
			case XWII_EVENT_NUNCHUK_KEY:
				handle_nunchuk_key(nunchuk, nunchuck_config, &info, &ev, state, dev->info);
				break;
			case XWII_EVENT_NUNCHUK_MOVE:
				handle_nunchuk_analog_stick(nunchuk, nunchuck_config, &info, &ev, state, dev->info);
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

	ret = xwii_iface_open(dev->iface, xwii_iface_available(dev->iface));

	if (ret)
		xf86IDrvMsg(dev->info, X_INFO, "Cannot open all requested interfaces\n");

	ret = xwii_iface_watch(dev->iface, true);
	if (ret)
		xf86IDrvMsg(dev->info, X_ERROR, "Cannot watch device for hotplug events\n");

	info->fd = xwii_iface_get_fd(dev->iface);
	if (info->fd >= 0) {
		dev->handler = xf86AddInputHandler(info->fd, xwiimote_input, dev);
	} else {
		xf86IDrvMsg(dev->info, X_ERROR, "Cannot get interface fd\n");
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
		xwii_iface_watch(dev->iface, false);
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
	const char *root, *snum, *driver, *subs;
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

	driver = udev_device_get_driver(p);
	subs = udev_device_get_subsystem(p);
	if (!driver || strcmp(driver, "wiimote") ||
		!subs || strcmp(subs, "hid")) {
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


static void xwiimote_configure_analog_sticks(struct xwiimote_dev *dev)
{
	memcpy(dev->map_analog_stick[ANALOG_STICK_NUNCHUK], map_analog_stick_nunchuk_default, sizeof(map_analog_stick_nunchuk_default));
	parse_analog_stick_config(dev, "MapNunchukAnalogStickAxis", &dev->map_analog_stick[ANALOG_STICK_NUNCHUK][KEYSET_NORMAL], "nunchuk analog stick");
	parse_analog_stick_config(dev, "MapNunchukIRAnalogStickAxis", &dev->map_analog_stick[ANALOG_STICK_NUNCHUK][KEYSET_IR], "nunchuk analog stick [ir mode]");

	memcpy(dev->map_analog_stick[ANALOG_STICK_LEFT], map_analog_stick_left_default, sizeof(map_analog_stick_left_default));
	parse_analog_stick_config(dev, "MapClassicAnalogStickAxis", &dev->map_analog_stick[ANALOG_STICK_LEFT][KEYSET_NORMAL], "left analog stick");
	parse_analog_stick_config(dev, "MapIRClassicAnalogStickAxis", &dev->map_analog_stick[ANALOG_STICK_LEFT][KEYSET_IR], "left analog stick [ir mode]");

	memcpy(dev->map_analog_stick[ANALOG_STICK_RIGHT], map_analog_stick_right_default, sizeof(map_analog_stick_right_default));
	parse_analog_stick_config(dev, "MapClassicAnalogStickAxisZ", &dev->map_analog_stick[ANALOG_STICK_RIGHT][KEYSET_NORMAL], "right analog stick");
	parse_analog_stick_config(dev, "MapClassicIRAnalogStickAxisZ", &dev->map_analog_stick[ANALOG_STICK_RIGHT][KEYSET_IR], "right analog stick [ir mode]");
}


static void xwiimote_configure_keys(struct xwiimote_dev *dev)
{
	const char *motion, *key;

	memcpy(dev->map_key[KEYSET_NORMAL], map_key_default, sizeof(map_key_default));
	memcpy(dev->map_key[KEYSET_IR], map_key_default, sizeof(map_key_default));

	motion = xf86FindOptionValue(dev->info->options, "MotionSource");
	if (!motion)
		motion = "";

	if (!strcasecmp(motion, "accelerometer")) {
		dev->motion = MOTION_ABS;
		dev->motion_source = SOURCE_ACCEL;
		dev->ifs |= XWII_IFACE_ACCEL;
	} else if (!strcasecmp(motion, "ir")) {
		dev->motion = MOTION_ABS;
		dev->motion_source = SOURCE_IR;
		dev->ifs |= XWII_IFACE_IR;
	} else if (!strcasecmp(motion, "motionplus")) {
		dev->motion = MOTION_REL;
		dev->motion_source = SOURCE_MOTIONPLUS;
		dev->ifs |= XWII_IFACE_MOTION_PLUS;
	}

	key = xf86FindOptionValue(dev->info->options, "MapLeft");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_LEFT]);

	key = xf86FindOptionValue(dev->info->options, "MapRight");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_RIGHT]);

	key = xf86FindOptionValue(dev->info->options, "MapUp");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_UP]);

	key = xf86FindOptionValue(dev->info->options, "MapDown");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_DOWN]);

	key = xf86FindOptionValue(dev->info->options, "MapA");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_A]);

	key = xf86FindOptionValue(dev->info->options, "MapB");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_B]);

	key = xf86FindOptionValue(dev->info->options, "MapPlus");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_PLUS]);

	key = xf86FindOptionValue(dev->info->options, "MapMinus");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_MINUS]);

	key = xf86FindOptionValue(dev->info->options, "MapHome");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_HOME]);

	key = xf86FindOptionValue(dev->info->options, "MapOne");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_ONE]);

	key = xf86FindOptionValue(dev->info->options, "MapTwo");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_TWO]);

	/* Nunchuk */

	key = xf86FindOptionValue(dev->info->options, "MapC");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_C]);

	key = xf86FindOptionValue(dev->info->options, "MapZ");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_Z]);

	/* Classic Controller and Pro Controller */

	key = xf86FindOptionValue(dev->info->options, "MapX");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_X]);

	key = xf86FindOptionValue(dev->info->options, "MapY");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_Y]);

	key = xf86FindOptionValue(dev->info->options, "MapTL");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_TL]);

	key = xf86FindOptionValue(dev->info->options, "MapTR");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_TR]);

	key = xf86FindOptionValue(dev->info->options, "MapZL");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_ZL]);

	key = xf86FindOptionValue(dev->info->options, "MapZR");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_ZR]);

   /* Pro Controller */

	key = xf86FindOptionValue(dev->info->options, "MapTHUMBL");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_THUMBL]);

	key = xf86FindOptionValue(dev->info->options, "MapTHUMBR");
	parse_key(dev, key, &dev->map_key[KEYSET_NORMAL][XWII_KEY_THUMBR]);

	/* IR Mode */

	key = xf86FindOptionValue(dev->info->options, "MapIRLeft");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_LEFT]);

	key = xf86FindOptionValue(dev->info->options, "MapIRRight");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_RIGHT]);

	key = xf86FindOptionValue(dev->info->options, "MapIRUp");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_UP]);

	key = xf86FindOptionValue(dev->info->options, "MapIRDown");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_DOWN]);

	key = xf86FindOptionValue(dev->info->options, "MapIRA");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_A]);

	key = xf86FindOptionValue(dev->info->options, "MapIRB");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_B]);

	key = xf86FindOptionValue(dev->info->options, "MapIRPlus");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_PLUS]);

	key = xf86FindOptionValue(dev->info->options, "MapIRMinus");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_MINUS]);

	key = xf86FindOptionValue(dev->info->options, "MapIRHome");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_HOME]);

	key = xf86FindOptionValue(dev->info->options, "MapIROne");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_ONE]);

	key = xf86FindOptionValue(dev->info->options, "MapIRTwo");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_TWO]);

	/* Nunchuk */

	key = xf86FindOptionValue(dev->info->options, "MapIRC");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_C]);

	key = xf86FindOptionValue(dev->info->options, "MapIRZ");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_Z]);

	/* Classic Controller and Pro Controler */

	key = xf86FindOptionValue(dev->info->options, "MapIRX");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_X]);

	key = xf86FindOptionValue(dev->info->options, "MapIRY");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_Y]);

	key = xf86FindOptionValue(dev->info->options, "MapIRTL");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_TL]);

	key = xf86FindOptionValue(dev->info->options, "MapIRTR");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_TR]);

	key = xf86FindOptionValue(dev->info->options, "MapIRZL");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_ZL]);

	key = xf86FindOptionValue(dev->info->options, "MapIRZR");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_ZR]);

   /* Pro Controller */

	key = xf86FindOptionValue(dev->info->options, "MapIRTHUMBL");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_THUMBL]);

	key = xf86FindOptionValue(dev->info->options, "MapIRTHUMBR");
	parse_key(dev, key, &dev->map_key[KEYSET_IR][XWII_KEY_THUMBR]);
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

  wiimote_preinit(wiimote_config);

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

	dev->ifs = XWII_IFACE_CORE;
	ret = xwii_iface_new(&dev->iface, dev->root);
	if (ret) {
		xf86IDrvMsg(info, X_ERROR, "Cannot alloc interface\n");
		ret = BadValue;
		goto err_free;
	}

	xwiimote_add_dev(dev);
	xwiimote_configure_keys(dev);
	xwiimote_configure_analog_sticks(dev);
	xwiimote_configure_mp(dev);
	xwiimote_configure_ir(dev);

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
			xwii_iface_unref(dev->iface);
		}
		free(dev->root);
		free(dev);
		info->private = NULL;
	}

	xf86DeleteInput(info, flags);
}

static const char *xwiimote_defaults[] = {
	"XkbRules", "evdev",
	"XkbModel", "evdev",
	"XkbLayout", "us",
	NULL
};

_X_EXPORT InputDriverRec xwiimote_driver = {
	1,
	xwiimote_name,
	NULL,
	xwiimote_preinit,
	xwiimote_uninit,
	NULL,
	xwiimote_defaults,
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
