#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <xcb/xcb.h>
#include <xcb/xinput.h>


static int _mode = 0;


static void
get_atom_names(xcb_connection_t *connection, xcb_atom_t atoms[], int len, char*** out) {
  xcb_get_atom_name_cookie_t *cookies = NULL;
  xcb_get_atom_name_reply_t *reply = NULL;
  int i;


  *out = calloc(len, sizeof(char**));
  cookies = calloc(len, sizeof(xcb_get_atom_name_cookie_t));
  if (cookies == NULL) goto out;

  for (i = 0; i < len; i++) {
    cookies[i] = xcb_get_atom_name(connection, atoms[i]);
  }

  for (i = 0; i < len; i++) {
    reply = xcb_get_atom_name_reply(connection, cookies[i], NULL);
    (*out)[i] = strdup(xcb_get_atom_name_name(reply));
    free(reply); reply = NULL;
  }
  
out:

  if (cookies != NULL) free(cookies);
}

 
static xcb_input_list_input_devices_reply_t *
get_input_devices(xcb_connection_t *connection) {
  xcb_input_list_input_devices_cookie_t cookie;
  cookie = xcb_input_list_input_devices(connection);
  return xcb_input_list_input_devices_reply(connection, cookie, NULL);
}

static void
get_device_properties(xcb_connection_t *connection, uint8_t device_id, xcb_atom_t **atoms, int *len) {
  xcb_input_list_device_properties_cookie_t cookie;
  xcb_input_list_device_properties_reply_t *reply = NULL;
  xcb_atom_t *a;
  int i;

  cookie = xcb_input_list_device_properties(connection, device_id);
  reply = xcb_input_list_device_properties_reply(connection, cookie, NULL);
   
  *len = xcb_input_list_device_properties_atoms_length(reply);
  *atoms = malloc(*len * sizeof(xcb_atom_t));

  a = xcb_input_list_device_properties_atoms(reply);

  for (i = 0; i < *len; i++) {
    (*atoms)[i] = a[i]; 
  }

  free(reply); reply = NULL;
}

static void set_wiimote_mode(xcb_connection_t *connection, int32_t mode) {
    xcb_input_list_input_devices_reply_t *reply = NULL;
    xcb_input_device_info_t *device;
    uint8_t device_id;
    xcb_atom_t *atoms = NULL;
    int len = 0;
    char **names = NULL;
    int i;

    if (mode == _mode) return;

    _mode = mode;

    reply = get_input_devices(connection);

    {
      xcb_input_device_info_iterator_t devices_iter;
      devices_iter = xcb_input_list_input_devices_devices_iterator (reply);

      while (devices_iter.rem) {
        device = devices_iter.data;
        device_id = device->device_id;
        get_device_properties(connection, device_id, &atoms, &len);
        get_atom_names(connection, atoms, len, &names);

        for (i = 0; i < len; i++) {
          if (names[i] && !strcmp(names[i], "Wii Remote Mode")) {
            xcb_input_change_device_property(
              connection,
              atoms[i],
              XCB_ATOM_INTEGER,
              device_id,
              XCB_INPUT_PROPERTY_FORMAT_32_BITS,
              XCB_PROP_MODE_REPLACE,
              1,
              &_mode);
          }
        }

        xcb_input_device_info_next(&devices_iter);
      }
    }

    if (atoms) free(atoms);
    for (i = 0; i < len; i++) {
      free(names[i]);
    }
    if (names) free(names); names = NULL;
    if (reply) free(reply);
}


static xcb_window_t get_active_window(xcb_connection_t *connection) {
  xcb_screen_t *screen;
  xcb_query_pointer_cookie_t cookie;
  xcb_query_pointer_reply_t *reply = NULL;
  xcb_window_t active_window = XCB_NONE;

  screen = xcb_setup_roots_iterator (xcb_get_setup (connection)).data;
  if (!screen) goto out;
  cookie = xcb_query_pointer(connection, screen->root); 
  reply = xcb_query_pointer_reply(connection, cookie, NULL);
  if (!reply) goto out;
  active_window = reply->child;

  if (reply->mask) {
    active_window = XCB_NONE;
    goto out;
  }

out:

  if (reply) free(reply);

  return active_window;
}


static void sync_wiimote_mode(xcb_connection_t *connection) {
  xcb_window_t active_window;
  xcb_grab_pointer_cookie_t cookie;
  xcb_grab_pointer_reply_t *reply = NULL;

  active_window = get_active_window(connection);
  if (active_window == XCB_NONE) goto out;
  cookie = xcb_grab_pointer(connection, 0, active_window, 0, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME);
  reply = xcb_grab_pointer_reply(connection, cookie, NULL);
  if (!reply) goto out;

  switch(reply->status) {
    case XCB_GRAB_STATUS_SUCCESS:
      xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
      set_wiimote_mode(connection, 0);
      break;
    case XCB_GRAB_STATUS_ALREADY_GRABBED:
    case XCB_GRAB_STATUS_FROZEN:
      set_wiimote_mode(connection, 1);
      break;
    default:
      set_wiimote_mode(connection, 1);
      break;
  }

out:

  if (reply) free(reply);

  return;
}


int main(int argc, char* argv[]) {
  xcb_connection_t *connection = NULL;

  connection = xcb_connect (NULL, NULL);
  if (!connection) goto out;

  sync_wiimote_mode(connection);

out:

  if (connection) xcb_disconnect(connection);

  return 0;
}

