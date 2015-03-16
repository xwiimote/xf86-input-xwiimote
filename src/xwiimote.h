#ifndef XWIIMOTE
#define XWIIMOTE

#include "wiimote.h"
#include "nunchuk.h"

enum key_layout {
	KEY_LAYOUT_DEFAULT,
	KEY_LAYOUT_IR,
	KEY_LAYOUT_NUM
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
  enum key_state motion_layout;

	XkbRMLVOSet rmlvo;

  struct wiimote wiimote;
  struct wiimote_config wiimote_config[KEY_LAYOUT_NUM];
  struct nunchuk nunchuk;
  struct nunchuk_config nunchuk_config[KEY_LAYOUT_NUM];

  OsTimerPtr timer;
};

#endif
