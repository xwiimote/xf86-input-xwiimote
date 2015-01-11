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

#define MIN_KEYCODE 8

#define XWIIMOTE_ACCEL_HISTORY_NUM 12
#define XWIIMOTE_ACCEL_HISTORY_MOD 2

#define XWIIMOTE_IR_AVG_RADIUS 10
#define XWIIMOTE_IR_AVG_MAX_SAMPLES 8
#define XWIIMOTE_IR_AVG_MIN_SAMPLES 4
#define XWIIMOTE_IR_AVG_WEIGHT 3
#define XWIIMOTE_IR_CONTINUOUS_SCROLL_BORDER 20
#define XWIIMOTE_IR_CONTINUOUS_SCROLL_MAX_X 10
#define XWIIMOTE_IR_CONTINUOUS_SCROLL_MAX_Y 10
#define XWIIMOTE_IR_MAX_Y 767
#define XWIIMOTE_IR_MAX_X 1023

#define XWIIMOTE_IR_KEYMAP_EXPIRY_SECS 1

#define XWIIMOTE_DISTSQ(ax, ay, bx, by) \
	((ax - bx) * (ax - bx) + (ay - by) * (ay - by))

#define ANALOG_STICK_SLICE_CENTER_ANGLE 360 / 8
#define ANALOG_STICK_SLICE_OUTER_ANGLE (180 - ANALOG_STICK_SLICE_CENTER_ANGLE) / 2

#define ANALOG_STICK_AMPLIFY_DEFAULT 3
#define ANALOG_STICK_DEADZONE_DEFAULT 40

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

enum analog_stick_type {
	ANALOG_STICK_LEFT,
	ANALOG_STICK_RIGHT,
	ANALOG_STICK_NUNCHUK,
	ANALOG_STICK_NUM,
};

struct analog_stick_axis {
	int32_t previous_value;
	double amplified;
	int32_t delta;
	int32_t previous_delta; /*MOTION_ABS*/
	double subpixel; /*MOTION_REL*/
};

struct analog_stick {
	struct analog_stick_axis x;
	struct analog_stick_axis y;
};

static int analog_stick_calculate_real_x(int x, int y) {
    double slope;
    double max_x;
    double real_x;
    double gradient_length;
    double gradient_slice;

    slope = y / x;

    if (slope > 1.0) {
      //scale changes for x
      max_x = y + (x/tan(ANALOG_STICK_SLICE_OUTER_ANGLE));

      gradient_length = 2 * (tan(ANALOG_STICK_SLICE_CENTER_ANGLE / 2) * max_x);
      gradient_slice = x / sin(ANALOG_STICK_SLICE_OUTER_ANGLE);

      real_x = gradient_slice / gradient_length * max_x;

    } else {
      //Map directly to to x axis
      real_x = x + (y / tan(ANALOG_STICK_SLICE_OUTER_ANGLE));
    }
    return real_x;
}

static int analog_stick_calculate_real_y(int x, int y) {
    return analog_stick_calculate_real_x(y, x);
}


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

struct analog_stick_axis_func {
	int mode;
	struct func map_high;
	struct func map_low;
	int32_t deadzone;
	double amplify;
};

struct analog_stick_func {
	struct analog_stick_axis_func x;
	struct analog_stick_axis_func y;
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

static struct analog_stick_func map_analog_stick_nunchuk_default[KEYSET_NUM] = {
	[KEYSET_NORMAL] = {
		.x = {
			.mode = MOTION_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_D,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_A,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
		.y = {
			.mode = MOTION_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_W,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_S,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
	},

	[KEYSET_IR] = {
		.x = {
			.mode = MOTION_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_D,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_A,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
		.y = {
			.mode = MOTION_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_W,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_S,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
	},
};

static struct analog_stick_func map_analog_stick_left_default[KEYSET_NUM] = {
	[KEYSET_NORMAL] = {
		.x = {
			.mode = MOTION_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_D,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_A,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
		.y = {
			.mode = MOTION_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_W,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_S,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
	},

	[KEYSET_IR] = {
		.x = {
			.mode = MOTION_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_D,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_A,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
		.y = {
			.mode = MOTION_NONE,
			.map_high = {
				.type = FUNC_KEY,
				.u.key = KEY_W,
			},
			.map_low = {
				.type = FUNC_KEY,
				.u.key = KEY_S,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
	},
};

static struct analog_stick_func map_analog_stick_right_default[KEYSET_NUM] = {
	[KEYSET_NORMAL] = {
		.x = {
			.mode = MOTION_REL,
			.map_high = {
				.type = FUNC_IGNORE,
			},
			.map_low = {
				.type = FUNC_IGNORE,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
		.y = {
			.mode = MOTION_REL,
			.map_high = {
				.type = FUNC_IGNORE,
			},
			.map_low = {
				.type = FUNC_IGNORE,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
	},
	[KEYSET_IR] = {
		.x = {
			.mode = MOTION_REL,
			.map_high = {
				.type = FUNC_IGNORE,
			},
			.map_low = {
				.type = FUNC_IGNORE,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
		.y = {
			.mode = MOTION_REL,
			.map_high = {
				.type = FUNC_IGNORE,
			},
			.map_low = {
				.type = FUNC_IGNORE,
			},
			.amplify = ANALOG_STICK_AMPLIFY_DEFAULT,
			.deadzone = ANALOG_STICK_DEADZONE_DEFAULT,
		},
	},
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
	struct func map_key[KEYSET_NUM][XWII_KEY_NUM];
	enum keyset key_pressed[XWII_KEY_NUM];

	struct analog_stick_func map_analog_stick[ANALOG_STICK_NUM][KEYSET_NUM];
	struct analog_stick analog_stick[ANALOG_STICK_NUM];
	enum keyset analog_direction_pressed[ANALOG_STICK_NUM][2];

	unsigned int mp_x;
	unsigned int mp_y;
	unsigned int mp_z;
	int mp_x_scale;
	int mp_y_scale;
	int mp_z_scale;

	struct timeval ir_last_valid_event;
	int ir_vec_x;
	int ir_vec_y;
	int ir_ref_x;
	int ir_ref_y;
	int ir_avg_x;
	int ir_avg_y;
	int ir_avg_count;

	int ir_avg_radius;
	int ir_avg_max_samples;
	int ir_avg_min_samples;
	int ir_avg_weight;
	int ir_keymap_expiry_secs;
	int ir_continuous_scroll_border;
  int ir_continuous_scroll_max_x;
  int ir_continuous_scroll_max_y;
  double ir_continuous_scroll_subpixel_x;
  double ir_continuous_scroll_subpixel_y;

  int ir_smooth_scroll_x;
  int ir_smooth_scroll_y;

	struct xwii_event_abs accel_history_ev[XWIIMOTE_ACCEL_HISTORY_NUM];
	int accel_history_cur;
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


static BOOL xwiimote_is_ir(struct xwiimote_dev *dev, struct xwii_event *ev)
{
	return (ev->time.tv_sec < dev->ir_last_valid_event.tv_sec + dev->ir_keymap_expiry_secs
			|| (ev->time.tv_sec == dev->ir_last_valid_event.tv_sec + dev->ir_keymap_expiry_secs
				&& ev->time.tv_usec < dev->ir_last_valid_event.tv_usec));
}

static void xwiimote_key(struct xwiimote_dev *dev, struct xwii_event *ev)
{
	unsigned int code;
	unsigned int state;
	unsigned int key;
	int btn;
	int absolute = 0;
	enum keyset keyset = KEYSET_NORMAL;

	code = ev->v.key.code;
	state = ev->v.key.state;
	if (code >= XWII_KEY_NUM)
		return;
	if (state > 1)
		return;

	if (dev->motion == MOTION_ABS)
		absolute = 1;

	if (ev->v.key.state) {
		if (xwiimote_is_ir(dev, ev)) {
			keyset = KEYSET_IR;
		}
		dev->key_pressed[code] = keyset;
	} else {
		keyset = dev->key_pressed[code];
	}

	switch (dev->map_key[keyset][code].type) {
		case FUNC_BTN:
			btn = dev->map_key[keyset][code].u.btn;
			xf86PostButtonEvent(dev->info->dev, absolute, btn,
								state, 0, 0);
			break;
		case FUNC_KEY:
			key = dev->map_key[keyset][code].u.key + MIN_KEYCODE;
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
	int32_t x, y, r;
	int absolute, i;

	if (dev->motion_source != SOURCE_ACCEL)
		return;

	++dev->accel_history_cur;
	dev->accel_history_cur %= XWIIMOTE_ACCEL_HISTORY_NUM;
	dev->accel_history_ev[dev->accel_history_cur] = ev->v.abs[0];

	/* choose the smallest one */
	x = dev->accel_history_ev[0].x;
	y = dev->accel_history_ev[0].y;
	for (i = 1; i < XWIIMOTE_ACCEL_HISTORY_NUM; i++) {
		if (dev->accel_history_ev[i].x < x)
			x = dev->accel_history_ev[i].x;
		if (dev->accel_history_ev[i].y < y)
			y = dev->accel_history_ev[i].y;
	}

	/* limit values to make it more stable */
	r = x % XWIIMOTE_ACCEL_HISTORY_MOD;
	x -= r;
	r = y % XWIIMOTE_ACCEL_HISTORY_MOD;
	y -= r;

	absolute = dev->motion == MOTION_ABS;
	xf86PostMotionEvent(dev->info->dev, absolute, 0, 2, x, y);
}

static void xwiimote_ir(struct xwiimote_dev *dev, struct xwii_event *ev)
{
	struct xwii_event_abs *a, *b, *c, d;
	int absolute, i, dists[6];

	absolute = dev->motion == MOTION_ABS;

	if (dev->motion_source != SOURCE_IR)
		return;

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
				d.x = dev->ir_ref_x + dev->ir_vec_x;
				d.y = dev->ir_ref_y + dev->ir_vec_y;
				dists[0] = XWIIMOTE_DISTSQ(c->x, c->y, dev->ir_ref_x, dev->ir_ref_y);
				dists[1] = XWIIMOTE_DISTSQ(c->x, c->y, d.x, d.y);
				dists[2] = XWIIMOTE_DISTSQ(a->x, a->y, dev->ir_ref_x, dev->ir_ref_y);
				dists[3] = XWIIMOTE_DISTSQ(a->x, a->y, d.x, d.y);
				dists[4] = XWIIMOTE_DISTSQ(b->x, b->y, dev->ir_ref_x, dev->ir_ref_y);
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
		b->x = a->x - dev->ir_vec_x;
		b->y = a->y - dev->ir_vec_y;
		if (XWIIMOTE_DISTSQ(a->x, a->y, dev->ir_ref_x, dev->ir_ref_y)
				< XWIIMOTE_DISTSQ(b->x, b->y, dev->ir_ref_x, dev->ir_ref_y)) {
			b->x = a->x + dev->ir_vec_x;
			b->y = a->y + dev->ir_vec_y;
			dev->ir_ref_x = a->x;
			dev->ir_ref_y = a->y;
		} else {
			dev->ir_ref_x = b->x;
			dev->ir_ref_y = b->y;
		}
	} else {
		/* Record some data in case one of the points disappears */
		dev->ir_vec_x = b->x - a->x;
		dev->ir_vec_y = b->y - a->y;
		dev->ir_ref_x = a->x;
		dev->ir_ref_y = a->y;
	}

	/* Final point is the average of both points */
	a->x = (a->x + b->x) / 2;
	a->y = (a->y + b->y) / 2;

	/* Start averaging if the location is consistant */
	dev->ir_avg_x = (dev->ir_avg_x * dev->ir_avg_count + a->x) / (dev->ir_avg_count+1);
	dev->ir_avg_y = (dev->ir_avg_y * dev->ir_avg_count + a->y) / (dev->ir_avg_count+1);
	if (++dev->ir_avg_count > dev->ir_avg_max_samples)
		dev->ir_avg_count = dev->ir_avg_max_samples;
	if (XWIIMOTE_DISTSQ(a->x, a->y, dev->ir_avg_x, dev->ir_avg_y)
			< dev->ir_avg_radius * dev->ir_avg_radius) {
		if (dev->ir_avg_count >= dev->ir_avg_min_samples) {
			a->x = (a->x + dev->ir_avg_x * dev->ir_avg_weight) / (dev->ir_avg_weight+1);
			a->y = (a->y + dev->ir_avg_y * dev->ir_avg_weight) / (dev->ir_avg_weight+1);
		}
	} else {
		dev->ir_avg_count = 0;
	}

  /* Absolute scrolling */
  {
    int abs_x, abs_y;

    /* Calculate the absolute x value */
    abs_x = XWIIMOTE_IR_MAX_X - a->x;
    if (abs_x < dev->ir_continuous_scroll_border) {
      abs_x = dev->ir_continuous_scroll_border;
    } else if (abs_x > XWIIMOTE_IR_MAX_X - dev->ir_continuous_scroll_border) {
      abs_x = XWIIMOTE_IR_MAX_X - dev->ir_continuous_scroll_border;
    }

    /* Moves cursor smoothly to the point pointed at */
    if (dev->ir_smooth_scroll_x < 0) {
      dev->ir_smooth_scroll_x = abs_x;
    } else if (abs_x > dev->ir_smooth_scroll_x) {
      dev->ir_smooth_scroll_x += 1;
    } else if (abs_x < dev->ir_smooth_scroll_x) {
      dev->ir_smooth_scroll_x -= 1;
    }

    /* Calculate the absolute y value */
    abs_y = a->y;
    if (abs_y < dev->ir_continuous_scroll_border) {
      abs_y = dev->ir_continuous_scroll_border;
    } else if (abs_y > XWIIMOTE_IR_MAX_Y - dev->ir_continuous_scroll_border) {
      abs_y = XWIIMOTE_IR_MAX_Y - dev->ir_continuous_scroll_border;
    }

    /* Moves cursor smoothly to the point pointed at */
    if (dev->ir_smooth_scroll_y < 0) {
      dev->ir_smooth_scroll_y = abs_y;
    } else if (abs_y > dev->ir_smooth_scroll_y) {
      dev->ir_smooth_scroll_y += 1;
    } else if (abs_y < dev->ir_smooth_scroll_y) {
      dev->ir_smooth_scroll_y -= 1;
    }

    xf86PostMotionEvent(dev->info->dev, absolute, 0, 2, dev->ir_smooth_scroll_x, dev->ir_smooth_scroll_y);
  }

  /* Continuous scrolling at the edges of the screen */
  {
    int scroll_x, scroll_y;
    double x_scale, y_scale;

    x_scale = dev->ir_continuous_scroll_max_x / dev->ir_continuous_scroll_border;
    if (a->x < dev->ir_continuous_scroll_border) {
      dev->ir_continuous_scroll_subpixel_x += (-a->x) * x_scale;
    } else if (a->x > XWIIMOTE_IR_MAX_X - dev->ir_continuous_scroll_border) {
      dev->ir_continuous_scroll_subpixel_x += (a->x - (XWIIMOTE_IR_MAX_X - dev->ir_continuous_scroll_border)) * x_scale;
    } else {
      scroll_x = 0;
      dev->ir_continuous_scroll_subpixel_x = 0;
    }
    scroll_x = (int) dev->ir_continuous_scroll_subpixel_x;
    dev->ir_continuous_scroll_subpixel_x -= scroll_x; 

    y_scale = dev->ir_continuous_scroll_max_y / dev->ir_continuous_scroll_border;
    if (a->y < dev->ir_continuous_scroll_border) {
      dev->ir_continuous_scroll_subpixel_y += (-a->y) * y_scale;
    } else if (a->y > XWIIMOTE_IR_MAX_Y - dev->ir_continuous_scroll_border) {
      dev->ir_continuous_scroll_subpixel_y += (a->y - (XWIIMOTE_IR_MAX_Y - dev->ir_continuous_scroll_border)) * y_scale;
    } else {
      scroll_y = 0;
      dev->ir_continuous_scroll_subpixel_y = 0;
    }
    scroll_y = (int) dev->ir_continuous_scroll_subpixel_y;
    dev->ir_continuous_scroll_subpixel_y -= scroll_y; 

    xf86PostMotionEvent(dev->info->dev, 0, 0, 2, scroll_x, scroll_y);
  }

	dev->ir_last_valid_event = ev->time;
}

static int32_t get_mp_axis(struct xwiimote_dev *dev,
			   struct xwii_event *ev,
			   unsigned int axis)
{
	switch (axis) {
	case 0:
		axis = dev->mp_x;
		break;
	case 1:
		axis = dev->mp_y;
		break;
	case 2:
		axis = dev->mp_z;
		break;
	default:
		return 0;
	}

	switch (axis) {
	case 0:
		return ev->v.abs[0].x * dev->mp_x_scale;
	case 1:
		return ev->v.abs[0].y * dev->mp_y_scale;
	case 2:
		return ev->v.abs[0].z * dev->mp_z_scale;
	default:
		return 0;
	}
}

static void xwiimote_motionplus(struct xwiimote_dev *dev, struct xwii_event *ev)
{
	int32_t x, z;
	int absolute;

	absolute = dev->motion == MOTION_ABS;

	if (dev->motion_source == SOURCE_MOTIONPLUS) {
		x = get_mp_axis(dev, ev, 0) / 100;
		z = get_mp_axis(dev, ev, 2) / 100;
		xf86PostMotionEvent(dev->info->dev, absolute, 0, 2, x, z);
	}
}

static void xwiimote_refresh(struct xwiimote_dev *dev)
{
	int ret;

	ret = xwii_iface_open(dev->iface, dev->ifs);
	if (ret)
		xf86IDrvMsg(dev->info, X_INFO, "Cannot open all requested interfaces\n");
}

static void nunchuk_refresh(struct xwiimote_dev *dev)
{
	if (xwii_iface_available(dev->iface) & XWII_IFACE_NUNCHUK) {
		xwii_iface_open(dev->iface, XWII_IFACE_NUNCHUK);
	}
}

static void press_key(struct xwiimote_dev *dev, int absolute, struct func *map_key) {
	unsigned int key;
	int btn;

	switch (map_key->type) {
		case FUNC_BTN:
			btn = map_key->u.btn;
			xf86PostButtonEvent(dev->info->dev, absolute, btn,
								1, 0, 0);
			break;
		case FUNC_KEY:
			key = map_key->u.key + MIN_KEYCODE;
			xf86PostKeyboardEvent(dev->info->dev, key, 1);
			break;
		case FUNC_IGNORE:
		default:
			break;
	}
}

static void depress_key(struct xwiimote_dev *dev, int absolute, struct func *map_key) {
	unsigned int key;
	int btn;

	switch (map_key->type) {
		case FUNC_BTN:
			btn = map_key->u.btn;
			xf86PostButtonEvent(dev->info->dev, absolute, btn,
								0, 0, 0);
			break;
		case FUNC_KEY:
			key = map_key->u.key + MIN_KEYCODE;
			xf86PostKeyboardEvent(dev->info->dev, key, 0);
			break;
		case FUNC_IGNORE:
		default:
			break;
	}
}

static void handle_stick_axis(struct xwiimote_dev *dev, int32_t value, struct analog_stick_axis *axis, struct analog_stick_axis_func *config)
{
	int32_t pixel;

  if (abs(value) <= config->deadzone) {
    value = 0;
  }

	/* Set up scroll value */
	if (config->mode == MOTION_REL) {
	  axis->amplified = pow((value / config->deadzone), config->amplify);
    axis->previous_delta = axis->delta;
		axis->delta = (int) axis->amplified;
		axis->subpixel += axis->amplified - axis->delta;
		pixel = (int) axis->subpixel;
		if (pixel != 0) {
			axis->delta += pixel;
			axis->subpixel -= pixel;
		}
	} else if (config->mode == MOTION_ABS) {
    if (value != 0) {
      if (value < 0) {
        value += config->deadzone;
      } else {
        value -= config->deadzone;
      }
      value *= (100 / config->deadzone);
    }
	  axis->amplified = value * config->amplify;
		axis->delta = axis->amplified - axis->previous_delta;
    axis->previous_delta = axis->amplified;
	} else {
		axis->delta = 0;
    axis->previous_delta = 0;
	}

	/* Handle key mappings */

	if (value > config->deadzone && axis->previous_value <= config->deadzone) {
		press_key(dev, config->mode, &config->map_high);
		if (axis->previous_value < -config->deadzone) {
			depress_key(dev, config->mode, &config->map_low);
		}
	}
	// Axis moved to low 
	else if (value < -config->deadzone && axis->previous_value >= -config->deadzone) {
		press_key(dev, config->mode, &config->map_low);
		if (axis->previous_value > config->deadzone) {
			depress_key(dev, config->mode, &config->map_high);
		}
	}
	// Axis moved to rest
	else if (value >= -config->deadzone && value <= config->deadzone) {
		if (axis->previous_value < -config->deadzone) {
			depress_key(dev, config->mode, &config->map_low);
		} else if (axis->previous_value > config->deadzone) {
			depress_key(dev, config->mode, &config->map_high);
		}
	}

	axis->previous_value = value;
}

static void xwiimote_nunchuk_stick(struct xwiimote_dev *dev, struct xwii_event *ev)
{
	struct analog_stick_func *config;
	struct analog_stick *stick;
	struct xwii_event_abs *abs;
	enum keyset keyset;
	BOOL is_ir;
  int real_x;
  int real_y;

	stick = &dev->analog_stick[ANALOG_STICK_NUNCHUK];
	config = dev->map_analog_stick[ANALOG_STICK_NUNCHUK];
	abs = &ev->v.abs[0];
	keyset = KEYSET_NORMAL;
	is_ir = xwiimote_is_ir(dev, ev);
  real_x = analog_stick_calculate_real_x(abs->x, abs->y);
  real_y = analog_stick_calculate_real_y(abs->x, abs->y);

	//Keep the prevous keyset of the previous button press, otherwise use what is appropriate
	keyset = dev->analog_direction_pressed[ANALOG_STICK_NUNCHUK][0];
	if (stick->x.previous_value > config[keyset].x.deadzone || stick->x.previous_value < -config[keyset].x.deadzone) {
		keyset = dev->analog_direction_pressed[ANALOG_STICK_NUNCHUK][0];
	} else {
		keyset = (is_ir) ? KEYSET_IR : KEYSET_NORMAL;
		dev->analog_direction_pressed[ANALOG_STICK_NUNCHUK][0] = keyset;
	}
	handle_stick_axis(dev, real_x, &stick->x, &config[keyset].x);		

	//Keep the prevous keyset of the previous button press, otherwise use what is appropriate
	keyset = dev->analog_direction_pressed[ANALOG_STICK_NUNCHUK][1];
	if (stick->y.previous_value > config[keyset].y.deadzone || stick->y.previous_value < -config[keyset].y.deadzone) {
		keyset = dev->analog_direction_pressed[ANALOG_STICK_NUNCHUK][1];
	} else {
		keyset = (is_ir) ? KEYSET_IR : KEYSET_NORMAL;
		dev->analog_direction_pressed[ANALOG_STICK_NUNCHUK][1] = keyset;
	}
	handle_stick_axis(dev, real_y, &stick->y, &config[keyset].y);	   

	/* Move the cursor if appropriate with the updated scroll values */
	if (config->x.mode != MOTION_NONE && config->y.mode != MOTION_NONE) {
		xf86IDrvMsg(dev->info, X_INFO, "BREAK current value x: %d, current value y: %d\n", stick->x.previous_value, stick->y.previous_value);
		xf86IDrvMsg(dev->info, X_INFO, "BREAK current x: %d, current y: %d\n", stick->x.delta, stick->y.delta);
		xf86IDrvMsg(dev->info, X_INFO, "BREAK previous x: %d, previous y: %d\n", stick->x.previous_delta, stick->y.previous_delta);
		xf86PostMotionEvent(dev->info->dev, 0, 0, 2, stick->x.delta, -stick->y.delta);
	}
}

static void xwiimote_nunchuk_key(struct xwiimote_dev *dev, struct xwii_event *ev)
{  
	unsigned int code;
	unsigned int state;
	unsigned int key;
	int btn;
	int absolute = 0;
	enum keyset keyset = KEYSET_NORMAL;

	code = ev->v.key.code;
	state = ev->v.key.state;
	if (code >= XWII_KEY_NUM)
		return;
	if (state > 1)
		return;

	if (dev->motion == MOTION_ABS)
		absolute = 1;

	switch (dev->map_key[keyset][code].type) {
		case FUNC_BTN:
			btn = dev->map_key[keyset][code].u.btn;
			xf86PostButtonEvent(dev->info->dev, absolute, btn,
								state, 0, 0);
			break;
		case FUNC_KEY:
			key = dev->map_key[keyset][code].u.key + MIN_KEYCODE;
			xf86PostKeyboardEvent(dev->info->dev, key, state);
			break;
		case FUNC_IGNORE:
		default:
			break;
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
		ret = xwii_iface_dispatch(dev->iface, &ev, sizeof(ev));
		if (ret)
			break;

		switch (ev.type) {
			case XWII_EVENT_WATCH:
				xwiimote_refresh(dev);
				nunchuk_refresh(dev);
				break;
			case XWII_EVENT_KEY:
				xwiimote_key(dev, &ev);
				break;
			case XWII_EVENT_ACCEL:
				xwiimote_accel(dev, &ev);
				break;
			case XWII_EVENT_IR:
				xwiimote_ir(dev, &ev);
			case XWII_EVENT_MOTION_PLUS:
				xwiimote_motionplus(dev, &ev);
				break;
			case XWII_EVENT_NUNCHUK_KEY:
				xwiimote_nunchuk_key(dev, &ev);
				break;
			case XWII_EVENT_NUNCHUK_MOVE:
				xwiimote_nunchuk_stick(dev, &ev);
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

static void parse_axis(struct xwiimote_dev *dev, const char *t,
			   unsigned int *out, unsigned int def)
{
	if (!t)
		return;

	if (!strcasecmp(t, "x"))
		*out = 0;
	else if (!strcasecmp(t, "y"))
		*out = 1;
	else if (!strcasecmp(t, "z"))
		*out = 2;
}

static void parse_scale(struct xwiimote_dev *dev, const char *t, int *out)
{
	if (!t)
		return;

	*out = atoi(t);
}


static void parse_analog_stick_axis_config(struct xwiimote_dev *dev, const char *value,
  struct analog_stick_axis_func *config, char const *stick_name, char const *axis_name)
{
	char const *c = value;
	char v[40];
	int i;
	double d;

  char low_mapping[30];
  char high_mapping[30];

	if (!value)
		return;

	while (*c != '\0') {
		/* Skip any possible whitespace */
		while ((*c == ' ' || *c == '\t') && *c != '\0') c++;

		if (sscanf(c, "mode=%40s", v)) {
			if (strcmp(v, "relative") == 0) {
				config->mode = MOTION_REL;
			} else if (strcmp(v, "absolute") == 0) {
				config->mode = MOTION_ABS;
			} else if (strcmp(v, "none") == 0) {
				config->mode = MOTION_NONE;
			} else {
				xf86Msg(X_WARNING, "%s %s: error parsing mode. value: %s\n", stick_name, axis_name, v);
			}
		} else if (sscanf(c, "keylow=%40s", v)) {
			parse_key(dev, v, &config->map_low);
		} else if (sscanf(c, "keyhigh=%40s", v)) {
			parse_key(dev, v, &config->map_high);
		} else if (sscanf(c, "deadzone=%i", &i)) {
			if (i > 1 && i < 100) {
				config->deadzone = i;
			} else {
				xf86Msg(X_WARNING, "%s %s: error parsing deadzone. value: %d\n", stick_name, axis_name, i);
			}
		} else if (sscanf(c, "amplify=%lf", &d)) {
			if (d >= 0.0  && d < 10.0) {
				config->amplify = d;
			} else {
				xf86Msg(X_WARNING, "%s %s: error parsing amplify. value: %f\n", stick_name, axis_name, d);
			}
		}

		/* Move past this option */
		while (*c != ' ' && *c != '\t' && *c != '\0') c++;
	}

  switch (config->map_low.type) {
    case FUNC_BTN:
      snprintf(low_mapping, sizeof(low_mapping), ", low-mapping=%d [button]", config->map_low.u.btn);
      break;
    case FUNC_KEY:
      snprintf(low_mapping, sizeof(low_mapping), ", high-mapping=%d [key]", config->map_low.u.key);
      break;
    default:
      low_mapping[0] = '\0';
      break;
  }

  switch (config->map_high.type) {
    case FUNC_BTN:
      snprintf(high_mapping, sizeof(high_mapping), ", high-mapping=%d [button]", config->map_high.u.btn);
      break;
    case FUNC_KEY:
      snprintf(high_mapping, sizeof(high_mapping), ", high-mapping=%d [key]", config->map_high.u.key);
      break;
    default:
      high_mapping[0] = '\0';
      break;
  }

  xf86Msg(X_INFO, "%s %s axis configured with mode=%d, deadzone=%d, amplify=%f%s%s\n", stick_name, axis_name, config->mode, config->deadzone, config->amplify, low_mapping, high_mapping);
}

static void parse_analog_stick_config(struct xwiimote_dev *dev,
	const char *key_prefix, struct analog_stick_func *config, char const *stick_name)
{
	const char *value;
	char key[100];

	if (!key_prefix)
		return;

	if (snprintf(key, 100, "%sX", key_prefix) < 100) {
		value = xf86FindOptionValue(dev->info->options, key);
		parse_analog_stick_axis_config (dev, value, &config->x, stick_name, "x");
	}

	if (snprintf(key, 100, "%sY", key_prefix) < 100) {
		value = xf86FindOptionValue(dev->info->options, key);
		parse_analog_stick_axis_config (dev, value, &config->y, stick_name, "y");
	}
}

static void xwiimote_configure_mp(struct xwiimote_dev *dev)
{
	const char *normalize, *factor, *t;
	int x, y, z, fac;

	/* TODO: Allow modifying x, y, z and factor via xinput-properties for
	 * run-time calibration. */

	factor = xf86FindOptionValue(dev->info->options, "MPCalibrationFactor");
	if (!factor)
		factor = "";

	if (!strcasecmp(factor, "on") ||
		!strcasecmp(factor, "true") ||
		!strcasecmp(factor, "yes"))
		fac = 50;
	else if (sscanf(factor, "%i", &fac) != 1)
		fac = 0;

	normalize = xf86FindOptionValue(dev->info->options, "MPNormalization");
	if (!normalize)
		normalize = "";

	if (!strcasecmp(normalize, "on") ||
		!strcasecmp(normalize, "true") ||
		!strcasecmp(normalize, "yes")) {
		xwii_iface_set_mp_normalization(dev->iface, 0, 0, 0, fac);
		xf86IDrvMsg(dev->info, X_INFO,
				"MP-Normalizer started with (0:0:0) * %i\n", fac);
	} else if (sscanf(normalize, "%i:%i:%i", &x, &y, &z) == 3) {
		xwii_iface_set_mp_normalization(dev->iface, x, y, z, fac);
		xf86IDrvMsg(dev->info, X_INFO,
				"MP-Normalizer started with (%i:%i:%i) * %i\n",
				x, y, z, fac);
	}

	t = xf86FindOptionValue(dev->info->options, "MPXAxis");
	parse_axis(dev, t, &dev->mp_x, 0);
	t = xf86FindOptionValue(dev->info->options, "MPXScale");
	parse_scale(dev, t, &dev->mp_x_scale);
	t = xf86FindOptionValue(dev->info->options, "MPYAxis");
	parse_axis(dev, t, &dev->mp_y, 1);
	t = xf86FindOptionValue(dev->info->options, "MPYScale");
	parse_scale(dev, t, &dev->mp_y_scale);
	t = xf86FindOptionValue(dev->info->options, "MPZAxis");
	parse_axis(dev, t, &dev->mp_z, 2);
	t = xf86FindOptionValue(dev->info->options, "MPZScale");
	parse_scale(dev, t, &dev->mp_z_scale);
}

static void xwiimote_configure_ir(struct xwiimote_dev *dev)
{
	const char *t;

	t = xf86FindOptionValue(dev->info->options, "IRAvgRadius");
	parse_scale(dev, t, &dev->ir_avg_radius);

	t = xf86FindOptionValue(dev->info->options, "IRAvgMaxSamples");
	parse_scale(dev, t, &dev->ir_avg_max_samples);
	if (dev->ir_avg_max_samples < 1) dev->ir_avg_max_samples = 1;

	t = xf86FindOptionValue(dev->info->options, "IRAvgMinSamples");
	parse_scale(dev, t, &dev->ir_avg_min_samples);
	if (dev->ir_avg_min_samples < 1) {
		dev->ir_avg_min_samples = 1;
	} else if (dev->ir_avg_min_samples > dev->ir_avg_max_samples) {
		dev->ir_avg_min_samples = dev->ir_avg_max_samples;
	}

	t = xf86FindOptionValue(dev->info->options, "IRAvgWeight");
	parse_scale(dev, t, &dev->ir_avg_weight);
	if (dev->ir_avg_weight < 0) dev->ir_avg_weight = 0;

	t = xf86FindOptionValue(dev->info->options, "IRKeymapExpirySecs");
	parse_scale(dev, t, &dev->ir_keymap_expiry_secs);

	t = xf86FindOptionValue(dev->info->options, "IRContinuousScrollBorder");
	parse_scale(dev, t, &dev->ir_continuous_scroll_border);

	t = xf86FindOptionValue(dev->info->options, "IRContinuousScrollMaxX");
	parse_scale(dev, t, &dev->ir_continuous_scroll_max_x);

	t = xf86FindOptionValue(dev->info->options, "IRContinuousScrollMaxY");
	parse_scale(dev, t, &dev->ir_continuous_scroll_max_y);
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
	dev->mp_x = 0;
	dev->mp_y = 1;
	dev->mp_z = 2;
	dev->mp_x_scale = 1;
	dev->mp_y_scale = 1;
	dev->mp_z_scale = 1;
	dev->ir_avg_radius = XWIIMOTE_IR_AVG_RADIUS;
	dev->ir_avg_max_samples = XWIIMOTE_IR_AVG_MAX_SAMPLES;
	dev->ir_avg_min_samples = XWIIMOTE_IR_AVG_MIN_SAMPLES;
	dev->ir_avg_weight = XWIIMOTE_IR_AVG_WEIGHT;
	dev->ir_keymap_expiry_secs = XWIIMOTE_IR_KEYMAP_EXPIRY_SECS;
	dev->ir_continuous_scroll_border = XWIIMOTE_IR_CONTINUOUS_SCROLL_BORDER;
  dev->ir_continuous_scroll_max_x = XWIIMOTE_IR_CONTINUOUS_SCROLL_MAX_X;
  dev->ir_continuous_scroll_max_y = XWIIMOTE_IR_CONTINUOUS_SCROLL_MAX_Y;

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
