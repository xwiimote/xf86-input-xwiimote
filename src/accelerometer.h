#ifndef ACCELEROMETER
#define ACCELEROMETER

#include <xf86Xinput.h>

#include <xwiimote.h>

#define ACCELEROMETER_HISTORY_NUM 12
#define ACCELEROMETER_HISTORY_MOD 2

struct accelerometer {
	struct xwii_event_abs accel_history_ev[ACCELEROMETER_HISTORY_NUM];
	int accel_history_cur;
};


struct accelerometer_config {
};


void handle_accelerometer(struct accelerometer *accelerometer, struct accelerometer_config *config, struct xwii_event *ev, InputInfoPtr info);
void configure_accelerometer(struct accelerometer_config *config, char const *name, InputInfoPtr info);

#endif

