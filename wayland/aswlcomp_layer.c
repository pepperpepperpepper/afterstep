#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include "aswlcomp_internal.h"
static struct wlr_scene_tree *layer_tree_for(struct aswl_server *server, enum zwlr_layer_shell_v1_layer layer)
{
	switch (layer) {
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
		return server->layer_trees[layer];
	default:
		return server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_TOP];
	}
}
void apply_layer_struts_top(struct aswl_server *server, struct wlr_output *output, struct wlr_box *usable)
{
	if (server == NULL || server->output_layout == NULL || output == NULL || usable == NULL)
		return;

	const int spacing = 3; /* Match AfterStep-ish NoCollidesSpacing feel. */
	int top_limit = usable->y;

	struct wlr_output *default_output = wlr_output_layout_get_center_output(server->output_layout);
	if (default_output == NULL)
		default_output = output;

	struct aswl_layer_surface *ls;
	wl_list_for_each(ls, &server->layer_surfaces, link) {
		struct wlr_layer_surface_v1 *surf = ls->layer_surface;
		if (surf == NULL || surf->surface == NULL || ls->scene == NULL || ls->scene->tree == NULL)
			continue;
		if (!surf->initialized || !surf->surface->mapped)
			continue;

		struct wlr_output *target = surf->output;
		if (target == NULL)
			target = default_output;
		if (target != output)
			continue;

		/* Only treat surfaces that want to reserve space. */
		if (surf->current.exclusive_zone <= 0)
			continue;

		/* Only treat wide/short surfaces as top "bars" (avoid right-side panels). */
		int sw = (int)surf->surface->current.width;
		int sh = (int)surf->surface->current.height;
		if (sw <= 0 || sh <= 0 || sw < sh)
			continue;

		if ((surf->current.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) == 0)
			continue;

		int sx = 0;
		int sy = 0;
		(void)wlr_scene_node_coords(&ls->scene->tree->node, &sx, &sy);

		int bottom = sy + sh + spacing;
		if (bottom > top_limit)
			top_limit = bottom;
	}

	int max_y = usable->y + usable->height;
	if (top_limit > max_y)
		top_limit = max_y;

	int delta = top_limit - usable->y;
	if (delta > 0) {
		usable->y = top_limit;
		usable->height -= delta;
	}
}
static void handle_layer_surface_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, destroy);

	wl_list_remove(&ls->destroy.link);
	if (ls->surface_destroy_listener_added) {
		wl_list_remove(&ls->surface_destroy.link);
		ls->surface_destroy_listener_added = false;
	}
	if (ls->surface_listeners_added) {
		wl_list_remove(&ls->map.link);
		wl_list_remove(&ls->unmap.link);
		wl_list_remove(&ls->commit.link);
		ls->surface_listeners_added = false;
	}
	wl_list_remove(&ls->link);
	free(ls);
}
static void handle_layer_surface_surface_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, surface_destroy);
	if (ls == NULL)
		return;

	wl_list_remove(&ls->surface_destroy.link);
	ls->surface_destroy_listener_added = false;

	if (ls->surface_listeners_added) {
		wl_list_remove(&ls->map.link);
		wl_list_remove(&ls->unmap.link);
		wl_list_remove(&ls->commit.link);
		ls->surface_listeners_added = false;
	}
}
static void handle_layer_surface_map(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, map);
	arrange_layers(ls->server);
}
static void handle_layer_surface_unmap(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, unmap);
	arrange_layers(ls->server);
}
static void handle_layer_surface_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct aswl_layer_surface *ls = wl_container_of(listener, ls, commit);
	arrange_layers(ls->server);
}
void handle_new_layer_surface(struct wl_listener *listener, void *data)
{
	struct aswl_server *server = wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *layer_surface = data;

	struct aswl_layer_surface *ls = calloc(1, sizeof(*ls));
	if (ls == NULL)
		return;

	ls->server = server;
	ls->layer_surface = layer_surface;
	ls->scene = wlr_scene_layer_surface_v1_create(layer_tree_for(server, layer_surface->pending.layer), layer_surface);
	if (ls->scene == NULL) {
		free(ls);
		return;
	}

	wl_list_insert(&server->layer_surfaces, &ls->link);

	ls->destroy.notify = handle_layer_surface_destroy;
	wl_signal_add(&layer_surface->events.destroy, &ls->destroy);

	ls->map.notify = handle_layer_surface_map;
	wl_signal_add(&layer_surface->surface->events.map, &ls->map);

	ls->unmap.notify = handle_layer_surface_unmap;
	wl_signal_add(&layer_surface->surface->events.unmap, &ls->unmap);

	ls->commit.notify = handle_layer_surface_commit;
	wl_signal_add(&layer_surface->surface->events.commit, &ls->commit);
	ls->surface_listeners_added = true;

	ls->surface_destroy.notify = handle_layer_surface_surface_destroy;
	wl_signal_add(&layer_surface->surface->events.destroy, &ls->surface_destroy);
	ls->surface_destroy_listener_added = true;

	arrange_layers(server);
}
static void arrange_layers_for_output(struct aswl_output *out)
{
	if (out == NULL || out->server == NULL || out->wlr_output == NULL)
		return;

	struct aswl_server *server = out->server;
	struct wlr_output *output = out->wlr_output;

	struct wlr_box full = { 0 };
	wlr_output_layout_get_box(server->output_layout, output, &full);
	struct wlr_box usable = full;

	struct wlr_output *default_output = wlr_output_layout_get_center_output(server->output_layout);
	if (default_output == NULL)
		default_output = output;

	const enum zwlr_layer_shell_v1_layer order[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
		ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
	};

	for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
		enum zwlr_layer_shell_v1_layer layer = order[i];

		struct aswl_layer_surface *ls;
		wl_list_for_each(ls, &server->layer_surfaces, link) {
			struct wlr_layer_surface_v1 *surf = ls->layer_surface;
			if (surf == NULL)
				continue;

			struct wlr_output *target = surf->output;
			if (target == NULL)
				target = default_output;
			if (target != output)
				continue;

			if (surf->current.layer != layer)
				continue;

			if (!surf->initialized)
				continue;

			wlr_scene_layer_surface_v1_configure(ls->scene, &full, &usable);
		}
	}

	if (out->lock_rect != NULL) {
		wlr_scene_rect_set_size(out->lock_rect, full.width, full.height);
		wlr_scene_node_set_position(&out->lock_rect->node, full.x, full.y);
		wlr_scene_node_set_enabled(&out->lock_rect->node, server->session_locked);
	}

	out->full_box = full;
	out->usable_box = usable;
}
void aswl_layer_arrange(struct aswl_server *server)
{
	if (server == NULL || server->output_layout == NULL)
		return;

	struct aswl_output *out;
	wl_list_for_each(out, &server->outputs, link)
		arrange_layers_for_output(out);
}
