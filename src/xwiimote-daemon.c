/*
 * XWiimote
 *
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
 * Copyright (c) 2015 Zachary Dovel<zakkudo2@gmail.com>
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

#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <xcb/xcb.h>
#include <xcb/xinput.h>

#include <xorg-server.h>
#include <xf86.h>
#include <xf86_OSproc.h>

#include "ir.h"


struct config {
    Bool version;
    Bool help;
    char const * program;
};

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
    switch(_mode) {
      case IR_MODE_GAME:
        printf("Setting wiimotes to Game Mode\n");
        break;
      case IR_MODE_MENU:
        printf("Setting wiimotes to Menu Mode\n");
        break;
      default:
        printf("Setting wiimotes to %d Mode\n", _mode);
        break;
    }

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


static int run_daemon(void) {
  xcb_connection_t *connection = NULL;
  struct timespec req = {
    .tv_sec = 0,
    .tv_nsec = 100000000L,
  };
  struct timespec rem =  {
    .tv_sec = 0,
    .tv_nsec = 0,
  };

  while (1) {
    connection = xcb_connect (NULL, NULL);
    if (!connection) goto out;

    sync_wiimote_mode(connection);
out:

    if (connection) xcb_disconnect(connection);
    nanosleep(&req, &rem);
  }

  return 0;
}

static int try_fork_daemon(char const * program) {
  pid_t pid = fork();
  if (pid == 0) {
    run_daemon();
  } else if (pid > 0) {
    int status = 0;
    system(program);
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
  } else {
    fprintf(stderr, "There was an error forking the daemon from %s\n", program);
  }
  return 0;
}

static int show_help(void) {
  printf("USAGE: xwiimote-daemon [options] ... [program]\n\n"

        "  -v, --version   Print version information\n"
        "  -h, --help      Print this help dialog\n\n"

        "You can run wiimote-daemon directly with no options, or you can\n"
        "pass the program name to make wiimote-daemon automatically quit\n"
        "when the game is quit.\n\n"

        "This program automates switching of the ir mode between menu\n"
        "and game mode to allowing you to easily use your wiimote for standard\n"
        "3d PC gaming.\n\n"

        "Some games will require tweaking of their settings.  Portal requires\n"
        "switching the mouse to raw input.  Minecraft requires turning off\n"
        "fullscreen.\n\n"

        "The daemon works be recognizing xorg pointer grabs.  When the grab changes,\n"
        "the daemon will print the new state to facilitate debugging.  In the end\n"
        "this daemon is a ducktype for games that doen't support the linux wiimote\n"
        "driver, so most tinkering will need to be done game-by-game.");

  return 0;
}

static int show_version(void) {
  printf("TODO\n");
  return 0;
}

static int parse_arguments(struct config *config, int argc, char* argv[]) {
  int i = 1;
  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
      config->version = TRUE;
      break;
    } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      config->help = TRUE;
      break;
    } else {
      config->program = argv[i];
      break;
    }
  }

  return 0;
}

int main(int argc, char* argv[]) {
  struct config config = {0};
  int status = 0;

  if (argc == 1) {
    return run_daemon();
  } else if ((status = parse_arguments(&config, argc, argv))) {
    return status;
  }
  
  if (config.help) {
    return show_help();
  } else if (config.version) {
    return show_version();
  } else if (config.program) {
    return try_fork_daemon(config.program);
  }

  return show_help();
}

