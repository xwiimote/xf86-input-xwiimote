#ifndef NUNCHUK
#define NUNCHUK

#include <xwiimote.h>

#include "analog-stick.h"

enum nunchuk_key {
  NUNCHUK_KEY_C,
  NUNCHUK_KEY_Z,
  NUNCHUK_KEY_NUM,
};

struct nunchuk {
  struct analog_stick analog_stick;
  struct key keys[NUNCHUK_KEY_NUM];
};


struct nunchuk_config {
  struct analog_stick_config analog_stick;
  struct key_config keys[NUNCHUK_KEY_NUM];
};

void configure_nunchuk(struct nunchuk_config *config, char const * prefix, struct nunchuk_config *defaults, InputInfoPtr info);

void handle_nunchuk_analog_stick(struct nunchuk *nunchuk, struct nunchuk_config *config, struct xwii_event *ev, unsigned int state, InputInfoPtr info);
void handle_nunchuk_key(struct nunchuk *nunchuk, struct nunchuk_config *config, struct xwii_event *ev, unsigned int state, InputInfoPtr info);

#endif
