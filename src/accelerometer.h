#ifndef ACCELEROMETER
#define ACCELEROMETER

#include <xf86Xinput.h>

#include <xwiimote.h>

#define ACCELEROMETER_MIN_X -100
#define ACCELEROMETER_MAX_X 100

#define ACCELEROMETER_MIN_Y -100
#define ACCELEROMETER_MAX_Y 100

#define ACCELEROMETER_HISTORY_NUM 12
#define ACCELEROMETER_HISTORY_MOD 2

#define ACCELEROMETER_MAX_ANGLE_DELTA 0.1

struct accelerometer {
	struct xwii_event_abs accel_history_ev[ACCELEROMETER_HISTORY_NUM];
	int accel_history_cur;
  double angle;
  double smooth_rotate_angle;
};


struct accelerometer_config {
  double max_angle_delta;
};


void handle_accelerometer_event(struct accelerometer *accelerometer, struct accelerometer_config *config, struct xwii_event *ev, InputInfoPtr info);
void configure_accelerometer(struct accelerometer_config *config, char const *name, InputInfoPtr info);
void handle_accelerometer_timer(struct accelerometer *accelerometer, struct accelerometer_config *config, InputInfoPtr info);

#endif

