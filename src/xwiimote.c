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

#define MIN_KEYCODE 8

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
	} u;
};

static struct func map_key_default[XWII_KEY_NUM] = {
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
};

enum motion_type {
	MOTION_NONE,
	MOTION_ABS,
	MOTION_REL,
};

enum motion_source {
	SOURCE_NONE,
	SOURCE_ACCEL,
};

struct xwiimote_dev {
	InputInfoPtr info;
	void *handler;
	int dev_id;
	char *root;
	const char *device;
	bool dup;
	struct xwii_iface *iface;

	XkbRMLVOSet rmlvo;
	unsigned int motion;
	unsigned int motion_source;
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
			btn = dev->map_key[code].u.btn;
			xf86PostButtonEvent(dev->info->dev, absolute, btn,
								state, 0, 0);
			break;
		case FUNC_KEY:
			key = dev->map_key[code].u.key + MIN_KEYCODE;
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
	int absolute;

	absolute = dev->motion;

	if (dev->motion_source == SOURCE_ACCEL) {
		x = ev->v.abs[0].x;
		y = -1 * ev->v.abs[0].y;
		xf86PostMotionEvent(dev->info->dev, absolute, 0, 2, x, y);
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

static struct key_value_pair {
	const char *key;
	unsigned int value;
} key2value[] = {
	{ "KEY_ESC", 1 },
	{ "KEY_1", 2 },
	{ "KEY_2", 3 },
	{ "KEY_3", 4 },
	{ "KEY_4", 5 },
	{ "KEY_5", 6 },
	{ "KEY_6", 7 },
	{ "KEY_7", 8 },
	{ "KEY_8", 9 },
	{ "KEY_9", 10 },
	{ "KEY_0", 11 },
	{ "KEY_MINUS", 12 },
	{ "KEY_EQUAL", 13 },
	{ "KEY_BACKSPACE", 14 },
	{ "KEY_TAB", 15 },
	{ "KEY_Q", 16 },
	{ "KEY_W", 17 },
	{ "KEY_E", 18 },
	{ "KEY_R", 19 },
	{ "KEY_T", 20 },
	{ "KEY_Y", 21 },
	{ "KEY_U", 22 },
	{ "KEY_I", 23 },
	{ "KEY_O", 24 },
	{ "KEY_P", 25 },
	{ "KEY_LEFTBRACE", 26 },
	{ "KEY_RIGHTBRACE", 27 },
	{ "KEY_ENTER", 28 },
	{ "KEY_LEFTCTRL", 29 },
	{ "KEY_A", 30 },
	{ "KEY_S", 31 },
	{ "KEY_D", 32 },
	{ "KEY_F", 33 },
	{ "KEY_G", 34 },
	{ "KEY_H", 35 },
	{ "KEY_J", 36 },
	{ "KEY_K", 37 },
	{ "KEY_L", 38 },
	{ "KEY_SEMICOLON", 39 },
	{ "KEY_APOSTROPHE", 40 },
	{ "KEY_GRAVE", 41 },
	{ "KEY_LEFTSHIFT", 42 },
	{ "KEY_BACKSLASH", 43 },
	{ "KEY_Z", 44 },
	{ "KEY_X", 45 },
	{ "KEY_C", 46 },
	{ "KEY_V", 47 },
	{ "KEY_B", 48 },
	{ "KEY_N", 49 },
	{ "KEY_M", 50 },
	{ "KEY_COMMA", 51 },
	{ "KEY_DOT", 52 },
	{ "KEY_SLASH", 53 },
	{ "KEY_RIGHTSHIFT", 54 },
	{ "KEY_KPASTERISK", 55 },
	{ "KEY_LEFTALT", 56 },
	{ "KEY_SPACE", 57 },
	{ "KEY_CAPSLOCK", 58 },
	{ "KEY_F1", 59 },
	{ "KEY_F2", 60 },
	{ "KEY_F3", 61 },
	{ "KEY_F4", 62 },
	{ "KEY_F5", 63 },
	{ "KEY_F6", 64 },
	{ "KEY_F7", 65 },
	{ "KEY_F8", 66 },
	{ "KEY_F9", 67 },
	{ "KEY_F10", 68 },
	{ "KEY_NUMLOCK", 69 },
	{ "KEY_SCROLLLOCK", 70 },
	{ "KEY_KP7", 71 },
	{ "KEY_KP8", 72 },
	{ "KEY_KP9", 73 },
	{ "KEY_KPMINUS", 74 },
	{ "KEY_KP4", 75 },
	{ "KEY_KP5", 76 },
	{ "KEY_KP6", 77 },
	{ "KEY_KPPLUS", 78 },
	{ "KEY_KP1", 79 },
	{ "KEY_KP2", 80 },
	{ "KEY_KP3", 81 },
	{ "KEY_KP0", 82 },
	{ "KEY_KPDOT", 83 },
	{ "KEY_ZENKAKUHANKAKU", 85 },
	{ "KEY_102ND", 86 },
	{ "KEY_F11", 87 },
	{ "KEY_F12", 88 },
	{ "KEY_RO", 89 },
	{ "KEY_KATAKANA", 90 },
	{ "KEY_HIRAGANA", 91 },
	{ "KEY_HENKAN", 92 },
	{ "KEY_KATAKANAHIRAGANA", 93 },
	{ "KEY_MUHENKAN", 94 },
	{ "KEY_KPJPCOMMA", 95 },
	{ "KEY_KPENTER", 96 },
	{ "KEY_RIGHTCTRL", 97 },
	{ "KEY_KPSLASH", 98 },
	{ "KEY_SYSRQ", 99 },
	{ "KEY_RIGHTALT", 100 },
	{ "KEY_LINEFEED", 101 },
	{ "KEY_HOME", 102 },
	{ "KEY_UP", 103 },
	{ "KEY_PAGEUP", 104 },
	{ "KEY_LEFT", 105 },
	{ "KEY_RIGHT", 106 },
	{ "KEY_END", 107 },
	{ "KEY_DOWN", 108 },
	{ "KEY_PAGEDOWN", 109 },
	{ "KEY_INSERT", 110 },
	{ "KEY_DELETE", 111 },
	{ "KEY_MACRO", 112 },
	{ "KEY_MUTE", 113 },
	{ "KEY_VOLUMEDOWN", 114 },
	{ "KEY_VOLUMEUP", 115 },
	{ "KEY_POWER", 116 },
	{ "KEY_KPEQUAL", 117 },
	{ "KEY_KPPLUSMINUS", 118 },
	{ "KEY_PAUSE", 119 },
	{ "KEY_SCALE", 120 },
	{ "KEY_KPCOMMA", 121 },
	{ "KEY_HANGEUL", 122 },
	{ "KEY_HANGUEL", 122 },
	{ "KEY_HANJA", 123 },
	{ "KEY_YEN", 124 },
	{ "KEY_LEFTMETA", 125 },
	{ "KEY_RIGHTMETA", 126 },
	{ "KEY_COMPOSE", 127 },
	{ "KEY_STOP", 128 },
	{ "KEY_AGAIN", 129 },
	{ "KEY_PROPS", 130 },
	{ "KEY_UNDO", 131 },
	{ "KEY_FRONT", 132 },
	{ "KEY_COPY", 133 },
	{ "KEY_OPEN", 134 },
	{ "KEY_PASTE", 135 },
	{ "KEY_FIND", 136 },
	{ "KEY_CUT", 137 },
	{ "KEY_HELP", 138 },
	{ "KEY_MENU", 139 },
	{ "KEY_CALC", 140 },
	{ "KEY_SETUP", 141 },
	{ "KEY_SLEEP", 142 },
	{ "KEY_WAKEUP", 143 },
	{ "KEY_FILE", 144 },
	{ "KEY_SENDFILE", 145 },
	{ "KEY_DELETEFILE", 146 },
	{ "KEY_XFER", 147 },
	{ "KEY_PROG1", 148 },
	{ "KEY_PROG2", 149 },
	{ "KEY_WWW", 150 },
	{ "KEY_MSDOS", 151 },
	{ "KEY_COFFEE", 152 },
	{ "KEY_SCREENLOCK", 152 },
	{ "KEY_DIRECTION", 153 },
	{ "KEY_CYCLEWINDOWS", 154 },
	{ "KEY_MAIL", 155 },
	{ "KEY_BOOKMARKS", 156 },
	{ "KEY_COMPUTER", 157 },
	{ "KEY_BACK", 158 },
	{ "KEY_FORWARD", 159 },
	{ "KEY_CLOSECD", 160 },
	{ "KEY_EJECTCD", 161 },
	{ "KEY_EJECTCLOSECD", 162 },
	{ "KEY_NEXTSONG", 163 },
	{ "KEY_PLAYPAUSE", 164 },
	{ "KEY_PREVIOUSSONG", 165 },
	{ "KEY_STOPCD", 166 },
	{ "KEY_RECORD", 167 },
	{ "KEY_REWIND", 168 },
	{ "KEY_PHONE", 169 },
	{ "KEY_ISO", 170 },
	{ "KEY_CONFIG", 171 },
	{ "KEY_HOMEPAGE", 172 },
	{ "KEY_REFRESH", 173 },
	{ "KEY_EXIT", 174 },
	{ "KEY_MOVE", 175 },
	{ "KEY_EDIT", 176 },
	{ "KEY_SCROLLUP", 177 },
	{ "KEY_SCROLLDOWN", 178 },
	{ "KEY_KPLEFTPAREN", 179 },
	{ "KEY_KPRIGHTPAREN", 180 },
	{ "KEY_NEW", 181 },
	{ "KEY_REDO", 182 },
	{ "KEY_F13", 183 },
	{ "KEY_F14", 184 },
	{ "KEY_F15", 185 },
	{ "KEY_F16", 186 },
	{ "KEY_F17", 187 },
	{ "KEY_F18", 188 },
	{ "KEY_F19", 189 },
	{ "KEY_F20", 190 },
	{ "KEY_F21", 191 },
	{ "KEY_F22", 192 },
	{ "KEY_F23", 193 },
	{ "KEY_F24", 194 },
	{ "KEY_PLAYCD", 200 },
	{ "KEY_PAUSECD", 201 },
	{ "KEY_PROG3", 202 },
	{ "KEY_PROG4", 203 },
	{ "KEY_DASHBOARD", 204 },
	{ "KEY_SUSPEND", 205 },
	{ "KEY_CLOSE", 206 },
	{ "KEY_PLAY", 207 },
	{ "KEY_FASTFORWARD", 208 },
	{ "KEY_BASSBOOST", 209 },
	{ "KEY_PRINT", 210 },
	{ "KEY_HP", 211 },
	{ "KEY_CAMERA", 212 },
	{ "KEY_SOUND", 213 },
	{ "KEY_QUESTION", 214 },
	{ "KEY_EMAIL", 215 },
	{ "KEY_CHAT", 216 },
	{ "KEY_SEARCH", 217 },
	{ "KEY_CONNECT", 218 },
	{ "KEY_FINANCE", 219 },
	{ "KEY_SPORT", 220 },
	{ "KEY_SHOP", 221 },
	{ "KEY_ALTERASE", 222 },
	{ "KEY_CANCEL", 223 },
	{ "KEY_BRIGHTNESSDOWN", 224 },
	{ "KEY_BRIGHTNESSUP", 225 },
	{ "KEY_MEDIA", 226 },
	{ "KEY_SWITCHVIDEOMODE", 227 },
	{ "KEY_KBDILLUMTOGGLE", 228 },
	{ "KEY_KBDILLUMDOWN", 229 },
	{ "KEY_KBDILLUMUP", 230 },
	{ "KEY_SEND", 231 },
	{ "KEY_REPLY", 232 },
	{ "KEY_FORWARDMAIL", 233 },
	{ "KEY_SAVE", 234 },
	{ "KEY_DOCUMENTS", 235 },
	{ "KEY_BATTERY", 236 },
	{ "KEY_BLUETOOTH", 237 },
	{ "KEY_WLAN", 238 },
	{ "KEY_UWB", 239 },
	{ "KEY_UNKNOWN", 240 },
	{ "KEY_VIDEO_NEXT", 241 },
	{ "KEY_VIDEO_PREV", 242 },
	{ "KEY_BRIGHTNESS_CYCLE", 243 },
	{ "KEY_BRIGHTNESS_ZERO", 244 },
	{ "KEY_DISPLAY_OFF", 245 },
	{ "KEY_WIMAX", 246 },
	{ "KEY_RFKILL", 247 },
	{ "KEY_MICMUTE", 248 },
	{ "BTN_MISC", 0x100 },
	{ "BTN_0", 0x100 },
	{ "BTN_1", 0x101 },
	{ "BTN_2", 0x102 },
	{ "BTN_3", 0x103 },
	{ "BTN_4", 0x104 },
	{ "BTN_5", 0x105 },
	{ "BTN_6", 0x106 },
	{ "BTN_7", 0x107 },
	{ "BTN_8", 0x108 },
	{ "BTN_9", 0x109 },
	{ "BTN_MOUSE", 0x110 },
	{ "BTN_LEFT", 0x110 },
	{ "BTN_RIGHT", 0x111 },
	{ "BTN_MIDDLE", 0x112 },
	{ "BTN_SIDE", 0x113 },
	{ "BTN_EXTRA", 0x114 },
	{ "BTN_FORWARD", 0x115 },
	{ "BTN_BACK", 0x116 },
	{ "BTN_TASK", 0x117 },
	{ "BTN_JOYSTICK", 0x120 },
	{ "BTN_TRIGGER", 0x120 },
	{ "BTN_THUMB", 0x121 },
	{ "BTN_THUMB2", 0x122 },
	{ "BTN_TOP", 0x123 },
	{ "BTN_TOP2", 0x124 },
	{ "BTN_PINKIE", 0x125 },
	{ "BTN_BASE", 0x126 },
	{ "BTN_BASE2", 0x127 },
	{ "BTN_BASE3", 0x128 },
	{ "BTN_BASE4", 0x129 },
	{ "BTN_BASE5", 0x12a },
	{ "BTN_BASE6", 0x12b },
	{ "BTN_DEAD", 0x12f },
	{ "BTN_GAMEPAD", 0x130 },
	{ "BTN_A", 0x130 },
	{ "BTN_B", 0x131 },
	{ "BTN_C", 0x132 },
	{ "BTN_X", 0x133 },
	{ "BTN_Y", 0x134 },
	{ "BTN_Z", 0x135 },
	{ "BTN_TL", 0x136 },
	{ "BTN_TR", 0x137 },
	{ "BTN_TL2", 0x138 },
	{ "BTN_TR2", 0x139 },
	{ "BTN_SELECT", 0x13a },
	{ "BTN_START", 0x13b },
	{ "BTN_MODE", 0x13c },
	{ "BTN_THUMBL", 0x13d },
	{ "BTN_THUMBR", 0x13e },
	{ "BTN_DIGI", 0x140 },
	{ "BTN_TOOL_PEN", 0x140 },
	{ "BTN_TOOL_RUBBER", 0x141 },
	{ "BTN_TOOL_BRUSH", 0x142 },
	{ "BTN_TOOL_PENCIL", 0x143 },
	{ "BTN_TOOL_AIRBRUSH", 0x144 },
	{ "BTN_TOOL_FINGER", 0x145 },
	{ "BTN_TOOL_MOUSE", 0x146 },
	{ "BTN_TOOL_LENS", 0x147 },
	{ "BTN_TOUCH", 0x14a },
	{ "BTN_STYLUS", 0x14b },
	{ "BTN_STYLUS2", 0x14c },
	{ "BTN_TOOL_DOUBLETAP", 0x14d },
	{ "BTN_TOOL_TRIPLETAP", 0x14e },
	{ "BTN_TOOL_QUADTAP", 0x14f },
	{ "BTN_WHEEL", 0x150 },
	{ "BTN_GEAR_DOWN", 0x150 },
	{ "BTN_GEAR_UP", 0x151 },
	{ "KEY_OK", 0x160 },
	{ "KEY_SELECT", 0x161 },
	{ "KEY_GOTO", 0x162 },
	{ "KEY_CLEAR", 0x163 },
	{ "KEY_POWER2", 0x164 },
	{ "KEY_OPTION", 0x165 },
	{ "KEY_INFO", 0x166 },
	{ "KEY_TIME", 0x167 },
	{ "KEY_VENDOR", 0x168 },
	{ "KEY_ARCHIVE", 0x169 },
	{ "KEY_PROGRAM", 0x16a },
	{ "KEY_CHANNEL", 0x16b },
	{ "KEY_FAVORITES", 0x16c },
	{ "KEY_EPG", 0x16d },
	{ "KEY_PVR", 0x16e },
	{ "KEY_MHP", 0x16f },
	{ "KEY_LANGUAGE", 0x170 },
	{ "KEY_TITLE", 0x171 },
	{ "KEY_SUBTITLE", 0x172 },
	{ "KEY_ANGLE", 0x173 },
	{ "KEY_ZOOM", 0x174 },
	{ "KEY_MODE", 0x175 },
	{ "KEY_KEYBOARD", 0x176 },
	{ "KEY_SCREEN", 0x177 },
	{ "KEY_PC", 0x178 },
	{ "KEY_TV", 0x179 },
	{ "KEY_TV2", 0x17a },
	{ "KEY_VCR", 0x17b },
	{ "KEY_VCR2", 0x17c },
	{ "KEY_SAT", 0x17d },
	{ "KEY_SAT2", 0x17e },
	{ "KEY_CD", 0x17f },
	{ "KEY_TAPE", 0x180 },
	{ "KEY_RADIO", 0x181 },
	{ "KEY_TUNER", 0x182 },
	{ "KEY_PLAYER", 0x183 },
	{ "KEY_TEXT", 0x184 },
	{ "KEY_DVD", 0x185 },
	{ "KEY_AUX", 0x186 },
	{ "KEY_MP3", 0x187 },
	{ "KEY_AUDIO", 0x188 },
	{ "KEY_VIDEO", 0x189 },
	{ "KEY_DIRECTORY", 0x18a },
	{ "KEY_LIST", 0x18b },
	{ "KEY_MEMO", 0x18c },
	{ "KEY_CALENDAR", 0x18d },
	{ "KEY_RED", 0x18e },
	{ "KEY_GREEN", 0x18f },
	{ "KEY_YELLOW", 0x190 },
	{ "KEY_BLUE", 0x191 },
	{ "KEY_CHANNELUP", 0x192 },
	{ "KEY_CHANNELDOWN", 0x193 },
	{ "KEY_FIRST", 0x194 },
	{ "KEY_LAST", 0x195 },
	{ "KEY_AB", 0x196 },
	{ "KEY_NEXT", 0x197 },
	{ "KEY_RESTART", 0x198 },
	{ "KEY_SLOW", 0x199 },
	{ "KEY_SHUFFLE", 0x19a },
	{ "KEY_BREAK", 0x19b },
	{ "KEY_PREVIOUS", 0x19c },
	{ "KEY_DIGITS", 0x19d },
	{ "KEY_TEEN", 0x19e },
	{ "KEY_TWEN", 0x19f },
	{ "KEY_VIDEOPHONE", 0x1a0 },
	{ "KEY_GAMES", 0x1a1 },
	{ "KEY_ZOOMIN", 0x1a2 },
	{ "KEY_ZOOMOUT", 0x1a3 },
	{ "KEY_ZOOMRESET", 0x1a4 },
	{ "KEY_WORDPROCESSOR", 0x1a5 },
	{ "KEY_EDITOR", 0x1a6 },
	{ "KEY_SPREADSHEET", 0x1a7 },
	{ "KEY_GRAPHICSEDITOR", 0x1a8 },
	{ "KEY_PRESENTATION", 0x1a9 },
	{ "KEY_DATABASE", 0x1aa },
	{ "KEY_NEWS", 0x1ab },
	{ "KEY_VOICEMAIL", 0x1ac },
	{ "KEY_ADDRESSBOOK", 0x1ad },
	{ "KEY_MESSENGER", 0x1ae },
	{ "KEY_DISPLAYTOGGLE", 0x1af },
	{ "KEY_SPELLCHECK", 0x1b0 },
	{ "KEY_LOGOFF", 0x1b1 },
	{ "KEY_DOLLAR", 0x1b2 },
	{ "KEY_EURO", 0x1b3 },
	{ "KEY_FRAMEBACK", 0x1b4 },
	{ "KEY_FRAMEFORWARD", 0x1b5 },
	{ "KEY_CONTEXT_MENU", 0x1b6 },
	{ "KEY_MEDIA_REPEAT", 0x1b7 },
	{ "KEY_10CHANNELSUP", 0x1b8 },
	{ "KEY_10CHANNELSDOWN", 0x1b9 },
	{ "KEY_IMAGES", 0x1ba },
	{ "KEY_DEL_EOL", 0x1c0 },
	{ "KEY_DEL_EOS", 0x1c1 },
	{ "KEY_INS_LINE", 0x1c2 },
	{ "KEY_DEL_LINE", 0x1c3 },
	{ "KEY_FN", 0x1d0 },
	{ "KEY_FN_ESC", 0x1d1 },
	{ "KEY_FN_F1", 0x1d2 },
	{ "KEY_FN_F2", 0x1d3 },
	{ "KEY_FN_F3", 0x1d4 },
	{ "KEY_FN_F4", 0x1d5 },
	{ "KEY_FN_F5", 0x1d6 },
	{ "KEY_FN_F6", 0x1d7 },
	{ "KEY_FN_F7", 0x1d8 },
	{ "KEY_FN_F8", 0x1d9 },
	{ "KEY_FN_F9", 0x1da },
	{ "KEY_FN_F10", 0x1db },
	{ "KEY_FN_F11", 0x1dc },
	{ "KEY_FN_F12", 0x1dd },
	{ "KEY_FN_1", 0x1de },
	{ "KEY_FN_2", 0x1df },
	{ "KEY_FN_D", 0x1e0 },
	{ "KEY_FN_E", 0x1e1 },
	{ "KEY_FN_F", 0x1e2 },
	{ "KEY_FN_S", 0x1e3 },
	{ "KEY_FN_B", 0x1e4 },
	{ "KEY_BRL_DOT1", 0x1f1 },
	{ "KEY_BRL_DOT2", 0x1f2 },
	{ "KEY_BRL_DOT3", 0x1f3 },
	{ "KEY_BRL_DOT4", 0x1f4 },
	{ "KEY_BRL_DOT5", 0x1f5 },
	{ "KEY_BRL_DOT6", 0x1f6 },
	{ "KEY_BRL_DOT7", 0x1f7 },
	{ "KEY_BRL_DOT8", 0x1f8 },
	{ "KEY_BRL_DOT9", 0x1f9 },
	{ "KEY_BRL_DOT10", 0x1fa },
	{ "KEY_NUMERIC_0", 0x200 },
	{ "KEY_NUMERIC_1", 0x201 },
	{ "KEY_NUMERIC_2", 0x202 },
	{ "KEY_NUMERIC_3", 0x203 },
	{ "KEY_NUMERIC_4", 0x204 },
	{ "KEY_NUMERIC_5", 0x205 },
	{ "KEY_NUMERIC_6", 0x206 },
	{ "KEY_NUMERIC_7", 0x207 },
	{ "KEY_NUMERIC_8", 0x208 },
	{ "KEY_NUMERIC_9", 0x209 },
	{ "KEY_NUMERIC_STAR", 0x20a },
	{ "KEY_NUMERIC_POUND", 0x20b },
	{ "KEY_CAMERA_FOCUS", 0x210 },
	{ "KEY_WPS_BUTTON", 0x211 },
	{ "KEY_TOUCHPAD_TOGGLE", 0x212 },
	{ "KEY_TOUCHPAD_ON", 0x213 },
	{ "KEY_TOUCHPAD_OFF", 0x214 },
	{ "KEY_CAMERA_ZOOMIN", 0x215 },
	{ "KEY_CAMERA_ZOOMOUT", 0x216 },
	{ "KEY_CAMERA_UP", 0x217 },
	{ "KEY_CAMERA_DOWN", 0x218 },
	{ "KEY_CAMERA_LEFT", 0x219 },
	{ "KEY_CAMERA_RIGHT", 0x21a },
	{ "BTN_TRIGGER_HAPPY", 0x2c0 },
	{ "BTN_TRIGGER_HAPPY1", 0x2c0 },
	{ "BTN_TRIGGER_HAPPY2", 0x2c1 },
	{ "BTN_TRIGGER_HAPPY3", 0x2c2 },
	{ "BTN_TRIGGER_HAPPY4", 0x2c3 },
	{ "BTN_TRIGGER_HAPPY5", 0x2c4 },
	{ "BTN_TRIGGER_HAPPY6", 0x2c5 },
	{ "BTN_TRIGGER_HAPPY7", 0x2c6 },
	{ "BTN_TRIGGER_HAPPY8", 0x2c7 },
	{ "BTN_TRIGGER_HAPPY9", 0x2c8 },
	{ "BTN_TRIGGER_HAPPY10", 0x2c9 },
	{ "BTN_TRIGGER_HAPPY11", 0x2ca },
	{ "BTN_TRIGGER_HAPPY12", 0x2cb },
	{ "BTN_TRIGGER_HAPPY13", 0x2cc },
	{ "BTN_TRIGGER_HAPPY14", 0x2cd },
	{ "BTN_TRIGGER_HAPPY15", 0x2ce },
	{ "BTN_TRIGGER_HAPPY16", 0x2cf },
	{ "BTN_TRIGGER_HAPPY17", 0x2d0 },
	{ "BTN_TRIGGER_HAPPY18", 0x2d1 },
	{ "BTN_TRIGGER_HAPPY19", 0x2d2 },
	{ "BTN_TRIGGER_HAPPY20", 0x2d3 },
	{ "BTN_TRIGGER_HAPPY21", 0x2d4 },
	{ "BTN_TRIGGER_HAPPY22", 0x2d5 },
	{ "BTN_TRIGGER_HAPPY23", 0x2d6 },
	{ "BTN_TRIGGER_HAPPY24", 0x2d7 },
	{ "BTN_TRIGGER_HAPPY25", 0x2d8 },
	{ "BTN_TRIGGER_HAPPY26", 0x2d9 },
	{ "BTN_TRIGGER_HAPPY27", 0x2da },
	{ "BTN_TRIGGER_HAPPY28", 0x2db },
	{ "BTN_TRIGGER_HAPPY29", 0x2dc },
	{ "BTN_TRIGGER_HAPPY30", 0x2dd },
	{ "BTN_TRIGGER_HAPPY31", 0x2de },
	{ "BTN_TRIGGER_HAPPY32", 0x2df },
	{ "BTN_TRIGGER_HAPPY33", 0x2e0 },
	{ "BTN_TRIGGER_HAPPY34", 0x2e1 },
	{ "BTN_TRIGGER_HAPPY35", 0x2e2 },
	{ "BTN_TRIGGER_HAPPY36", 0x2e3 },
	{ "BTN_TRIGGER_HAPPY37", 0x2e4 },
	{ "BTN_TRIGGER_HAPPY38", 0x2e5 },
	{ "BTN_TRIGGER_HAPPY39", 0x2e6 },
	{ "BTN_TRIGGER_HAPPY40", 0x2e7 },
	{ NULL, 0 },
};

static void parse_key(struct xwiimote_dev *dev, const char *key, struct func *out)
{
	unsigned int i;

	if (!key)
		return;

	if (!strcasecmp(key, "none") ||
			!strcasecmp(key, "off") ||
			!strcasecmp(key, "0") ||
			!strcasecmp(key, "false")) {
		out->type = FUNC_IGNORE;
	} else if (!strcasecmp(key, "left-button")) {
		out->type = FUNC_BTN;
		out->u.btn = 1;
	} else if (!strcasecmp(key, "right-button")) {
		out->type = FUNC_BTN;
		out->u.btn = 3;
	} else if (!strcasecmp(key, "middle-button")) {
		out->type = FUNC_BTN;
		out->u.btn = 2;
	} else {
		for (i = 0; key2value[i].key; ++i) {
			if (!strcasecmp(key2value[i].key, key))
				break;
		}

		if (key2value[i].key) {
			out->type = FUNC_KEY;
			out->u.key = key2value[i].value;
		} else {
			xf86IDrvMsg(dev->info, X_ERROR,
						"Invalid key option %s\n", key);
			out->type = FUNC_IGNORE;
		}
	}
}

static void xwiimote_configure(struct xwiimote_dev *dev)
{
	const char *motion, *key;

	memcpy(dev->map_key, map_key_default, sizeof(map_key_default));

	motion = xf86FindOptionValue(dev->info->options, "MotionSource");
	if (!motion)
		motion = "";

	if (!strcasecmp(motion, "accelerometer")) {
		dev->motion = MOTION_ABS;
		dev->motion_source = SOURCE_ACCEL;
	}

	key = xf86FindOptionValue(dev->info->options, "MapLeft");
	parse_key(dev, key, &dev->map_key[XWII_KEY_LEFT]);

	key = xf86FindOptionValue(dev->info->options, "MapRight");
	parse_key(dev, key, &dev->map_key[XWII_KEY_RIGHT]);

	key = xf86FindOptionValue(dev->info->options, "MapUp");
	parse_key(dev, key, &dev->map_key[XWII_KEY_UP]);

	key = xf86FindOptionValue(dev->info->options, "MapDown");
	parse_key(dev, key, &dev->map_key[XWII_KEY_DOWN]);

	key = xf86FindOptionValue(dev->info->options, "MapA");
	parse_key(dev, key, &dev->map_key[XWII_KEY_A]);

	key = xf86FindOptionValue(dev->info->options, "MapB");
	parse_key(dev, key, &dev->map_key[XWII_KEY_B]);

	key = xf86FindOptionValue(dev->info->options, "MapPlus");
	parse_key(dev, key, &dev->map_key[XWII_KEY_PLUS]);

	key = xf86FindOptionValue(dev->info->options, "MapMinus");
	parse_key(dev, key, &dev->map_key[XWII_KEY_MINUS]);

	key = xf86FindOptionValue(dev->info->options, "MapHome");
	parse_key(dev, key, &dev->map_key[XWII_KEY_HOME]);

	key = xf86FindOptionValue(dev->info->options, "MapOne");
	parse_key(dev, key, &dev->map_key[XWII_KEY_ONE]);

	key = xf86FindOptionValue(dev->info->options, "MapTwo");
	parse_key(dev, key, &dev->map_key[XWII_KEY_TWO]);
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
	xwiimote_configure(dev);

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
