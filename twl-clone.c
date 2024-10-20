#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <wlr/util/log.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pass.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

enum twl_clone_cursor_mode {
	TWL_CLONE_CURSOR_PASSTHROUGH,
};

struct twl_clone_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	enum twl_clone_cursor_mode cursor_mode;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_frame;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;
	
	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;
};

struct twl_clone_output {
	// what sorcery is this???
	struct wl_list link;

	struct wlr_output *wlr_output;
	struct twl_clone_server *server;

	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

struct twl_clone_keyboard {
	struct wl_list link;

	struct twl_clone_server *server;
	struct wlr_keyboard *wlr_keyboard;
};

static void output_frame(struct wl_listener *listener, void *data) {
	// Called every time an output wants to draw a frame (usually at output refresh rate).
	struct twl_clone_output *output = wl_container_of(listener, output, frame);
	struct wlr_output *wlr_output = output->wlr_output;

	// TODO: scene graph
	// wlr_log(WLR_INFO, "frame");
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	struct wlr_render_pass *pass = wlr_output_begin_render_pass(wlr_output, &state, NULL);
	wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
		.box = { .width = wlr_output->width, .height = wlr_output->height },
		.color = {
			.r = .5f,
			.g = .8f,
			.b = .3f,
			.a = 1.0f,
		},
	});

	wlr_render_pass_submit(pass);

	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);
}

static void output_request_state(struct wl_listener *listener, void *data) {
	// Handle requests to update the output state (like resize)
	struct twl_clone_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data) {
	struct twl_clone_output *output = wl_container_of(listener, output, destroy);

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
}

static void server_new_output(struct wl_listener *listener, void *data) {
	// wlr_log(WLR_INFO, "omg hey its a new output!");
	// This event is triggered when a new output is available.
	struct twl_clone_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	// Use the allocator and renderer from server.
	wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	// Enable the output (it may be off by default)
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	// Pick the monitor-preferred mode (w/h/refresh)
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}

	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	// Create our state struct for the output.
	struct twl_clone_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;

	// Listen to frame events (request to draw)
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	// Listen to state update events (WL/X11 resize)
	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);

	// Listen to destroy event (disconnection)
	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	wl_list_insert(&server->outputs, &output->link);


	// TODO: Add scene graph setupp here.

	// struct wlr_output_layout_output *l_output = wlr_output_layout_add_auto(server->output_layout, wlr_output);
	// struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, wlr_output);
	// wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);
}

static void process_cursor_motion(struct twl_clone_server *server, uint32_t time) {
	struct wlr_seat *seat = server->seat;
	// TODO: proxy events into child surface
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	wlr_seat_pointer_clear_focus(seat);
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
	// Triggered whenever a relative motion event ahppens (movement delta)
	struct twl_clone_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;

	// wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	// process_cursor_motion(server, event->time_msec);
	// wlr_log(WLR_INFO, "r pos %f, %f\n", event->delta_x, event->delta_y);
	printf("r pos %f, %f\n", event->delta_x, event->delta_y);
	// TODO: actually move the cursor maybe
}
static void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	// Triggered whenever an absolute motion event happens (LIKE IN A WAYLAND BACKEND!!!)
	struct twl_clone_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);

	// wlr_log(WLR_INFO, "a pos %f, %f\n", event->x, event->y);
	printf("a pos %f, %f\n", event->x, event->y);
	// process_cursor_motion(server, event->time_msec);
	// TODO: actually move the cursor maybe
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
	// Triggered when a pointer emits a frame event
	struct twl_clone_server *server = wl_container_of(listener ,server, cursor_frame);
	// wlr_log(WLR_INFO, "cursor frame\n");
	printf("cursor frame\n");
	wlr_seat_pointer_notify_frame(server->seat);
}


static void server_new_keyboard(struct twl_clone_server *server, struct wlr_input_device *device) {
	wlr_log(WLR_INFO, "new input is a keyboard!");
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct twl_clone_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	// TODO: setup keyboard keymap and events
	
	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(struct twl_clone_server *server, struct wlr_input_device *device) {
	wlr_log(WLR_INFO, "new input was a pointer!");
	// Just attach the pointer to the cursor ezpz
	wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data) {
	wlr_log(WLR_INFO, "new input!");
	struct twl_clone_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}

	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
	// Called whenever a WL client wants to change the cursor image.
	struct twl_clone_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;

	if (focused_client == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface, 
				event->hotspot_x, event->hotspot_y);
	}
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
	struct twl_clone_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
	//                                            ^^^          ^^^ voodoo
}

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_DEBUG, NULL);
	
	char *startup_cmd = NULL;
	int c;
	while ((c = getopt(argc, argv, "s:h")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		default:
			printf("Usage: %s [-s startup command]\n", argv[0]);
			return 0;
		}
	}
	if (optind < argc) {
		printf("Usage: %s [-s startup command]\n", argv[0]);
		return 0;
	}

	struct twl_clone_server server = {0};

	// Connection from WL clients to this WL server / compositor
	server.wl_display = wl_display_create();

	// Interface layer to base input/output hardware.
	server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), NULL);
	if (server.backend == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_backend");
		return 1;
	}

	// Renderer used by compositor to do compositing
	server.renderer = wlr_renderer_autocreate(server.backend);
	if (server.renderer == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_renderer");
		return 1;
	}

	wlr_renderer_init_wl_display(server.renderer, server.wl_display);

	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	if (server.allocator == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_allocator");
		return 1;
	}

	wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);

	// Multi-screen output support
	server.output_layout = wlr_output_layout_create(server.wl_display);

	// Handle "new output appeared" event.
	wl_list_init(&server.outputs);
	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	// TODO: Create scene graph to handle windows
	server.scene = wlr_scene_create();
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

	// TODO: Initialize xdg-shellv3 (used for application windows)

	// TODO: Add cursor support 
	
	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	server.cursor_mode = TWL_CLONE_CURSOR_PASSTHROUGH;
	server.cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
	server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);
	server.cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);


	// Seat setup
	wl_list_init(&server.keyboards);
	server.new_input.notify = server_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);

	server.seat = wlr_seat_create(server.wl_display, "seat0");
	server.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);
	server.request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);


	// Start the backend
	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	

	// Start the event loop
	wlr_log(WLR_INFO, "STARTINGGGGG");
	wl_display_run(server.wl_display);

	
	wlr_seat_destroy(server.seat);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_output_layout_destroy(server.output_layout);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.wl_display);

	return 0;
}
