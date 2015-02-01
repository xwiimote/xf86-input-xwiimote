#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <xcb/xcb.h>
#include <xcb/xinput.h>


void
get_atom_names(xcb_connection_t *connection, xcb_atom_t atoms[], int len, char*** out) {
  xcb_get_atom_name_cookie_t *cookies = NULL;
  xcb_get_atom_name_reply_t *reply;
  char *name = NULL;
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

 
xcb_input_list_input_devices_reply_t *
get_input_devices(xcb_connection_t *connection) {
  xcb_input_list_input_devices_cookie_t cookie;
  xcb_input_device_info_iterator_t iter;
  xcb_input_device_id_t input_device_id;

  cookie = xcb_input_list_input_devices(connection);
  return xcb_input_list_input_devices_reply(connection, cookie, NULL);
}

void
get_device_properties(xcb_connection_t *connection, uint8_t device_id, xcb_atom_t **atoms, int *len) {
  xcb_input_list_device_properties_cookie_t cookie;
  xcb_input_list_device_properties_reply_t *reply;
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

void set_wiimote_mode(xcb_connection_t *connection, int32_t mode) {
    xcb_input_list_input_devices_reply_t *devices_reply;
    xcb_input_device_info_t *device;
    uint8_t device_id;
    xcb_void_cookie_t cookie;
    int len;
    xcb_atom_t *atoms;
    char **names;
    int i;

    devices_reply = get_input_devices(connection);

    {
      xcb_input_device_info_iterator_t devices_iter;
      devices_iter = xcb_input_list_input_devices_devices_iterator (devices_reply);

      while (devices_iter.rem) {
        device = devices_iter.data;
        device_id = device->device_id;
        printf("%d\n", device_id);
        get_device_properties(connection, device_id, &atoms, &len);
        get_atom_names(connection, atoms, len, &names);

        for (i = 0; i < len; i++) {
          if (names[i] && !strcmp(names[i], "Wii Remote Mode")) {
            printf("name: %s, atom: %d, deviceid %d, mode: %d\n", names[i], atoms[i], device_id, mode);
            xcb_input_change_device_property(
              connection,
              atoms[i],
              XCB_ATOM_INTEGER,
              device_id,
              XCB_INPUT_PROPERTY_FORMAT_32_BITS,
              XCB_PROP_MODE_REPLACE,
              1,
              &mode);
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
    if (devices_reply) free(devices_reply);
}


int main(int argc, char* argv[]) {
    xcb_connection_t *connection;
    connection = xcb_connect (NULL, NULL);

    set_wiimote_mode(connection, 0);

    xcb_disconnect (connection);
    connection = NULL;

out:

    return 0;
}
