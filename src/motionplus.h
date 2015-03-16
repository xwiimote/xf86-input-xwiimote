#ifndef MOTIONPLUS
#define MOTIONPLUS

#include <xf86Xinput.h>
#include <xwiimote.h>

#define MOTIONPLUS_MAX_X 10000
#define MOTIONPLUS_MIN_X -10000

#define MOTIONPLUS_MAX_Y 10000
#define MOTIONPLUS_MIN_Y -10000

struct motionplus {
};

struct motionplus_config {
	unsigned int x;
	unsigned int y;
	unsigned int z;
	int x_scale;
	int y_scale;
	int z_scale;
  int x_normalization;
  int y_normalization;
  int z_normalization;
  int factor;
};

void preinit_motionplus(struct motionplus_config *config);
void configure_motionplus(struct motionplus_config *config, char const *prefix, InputInfoPtr info);
void handle_motionplus_event(struct motionplus *motionplus, struct motionplus_config *config, struct xwii_event *ev, InputInfoPtr info);

#endif
