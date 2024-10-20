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
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

struct twl_clone_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;


	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;
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

static void output_frame(struct wl_listener *listener, void *data) {
	// Called every time an output wants to draw a frame (usually at output refresh rate).
	struct twl_clone_output *output = wl_container_of(listener, output, frame);
	struct wlr_output *wlr_output = output->wlr_output;

	// TODO: scene graph
	wlr_log(WLR_INFO, "frame");
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



	// struct wlr_output_layout_output *l_output = wlr_output_layout_add_auto(server->output_layout, wlr_output);
	// struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, wlr_output);
	// wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);
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

	// Start the backend
	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	// Start the event loop
	wlr_log(WLR_INFO, "STARTINGGGGG");
	wl_display_run(server.wl_display);

	
	wlr_output_layout_destroy(server.output_layout);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.wl_display);

	return 0;
}
