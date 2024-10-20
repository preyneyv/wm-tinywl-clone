#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <wlr/util/log.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/allocator.h>
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

static void server_new_output(struct wl_listener *listener, void *data) {
	wlr_log(WLR_INFO, "omg hey its a new output!");
	// // This event is triggered when a new output is available.
	// struct twl_clone_server *server = wl_container_of(listener, server, new_output);
	// struct wlr_output *wlr_output = data;

	// // Use the allocator and renderer from server.
	// wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	// struct wlr_output_state state;
	// wlr_output_state_init(&state);
	// wlr_output_state_set_enabled(&state, true);

	
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
