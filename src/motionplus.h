#ifndef MOTIONPLUS
#define MOTIONPLUS

#include <xf86Xinput.h>
#include <xwiimote.h>

struct motionplus {
};

struct motionplus_config {
	unsigned int x;
	unsigned int y;
	unsigned int z;
	int x_scale;
	int y_scale;
	int z_scale;
};

void preinit_motionplus(struct motionplus_config *config);
void configure_motionplus(struct motionplus_config *config, InputInfoPtr info);
void handle_motionplus(struct motionplus *motionplus, struct motionplus_config *config, struct xwii_event *ev, InputInfoPtr info);

#endif
