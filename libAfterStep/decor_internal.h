#ifndef DECOR_INTERNAL_H
#define DECOR_INTERNAL_H

/* Shared private symbols for the decor.c family split. None of these are part
 * of the public decor.h API.
 *
 * The ASTileTypeHandlers dispatch table is defined in decor.c, alongside the
 * per-tile-type handler functions. Those handlers stay static in decor.c -
 * they are only ever named by the table initializer, never called by name, so
 * they never cross a TU boundary. Other TUs in the family reach the handlers
 * purely through this named struct type + the extern table (indirect dispatch
 * via ASTileTypeHandlers[type].<handler>(...)).
 *
 * Requires decor.h / mystyle.h / afterimage.h (ASTile, MyStyle, ASImageLayer,
 * ASImage, AS_TileTypes) to be included first. */

typedef struct ASTileTypeHandler {
	char *name;
	void (*free_astile_handler) (ASTile * tile);
	int (*check_point_handler) (ASTile * tile, int x, int y);
	void (*on_style_changed_handler) (ASTile * tile, MyStyle * style,
																		unsigned int state);
	int (*set_layer_handler) (ASTile * tile, ASImageLayer * layer,
														unsigned int state, ASImage ** scrap_images,
														int max_width, int max_height);
} ASTileTypeHandler;

extern ASTileTypeHandler ASTileTypeHandlers[AS_TileTypes];

/* currently-focused bar (balloon owner) - defined (de-static'd) in decor.c */
extern ASTBarData *FocusedBar;

/* helpers defined in decor.c, used by the astbar code in decor_astbar.c */
void build_btn_block (ASTile * tile,
											struct button_t **from_list, ASFlagType context_mask,
											unsigned int count, int left_margin, int top_margin,
											int spacing, int order);
Bool set_tbtn_pressed (ASBtnBlock * bb, int context);

/* helpers defined in decor_astbar.c, used by the rendering code in
 * decor_render.c. flush_tbar_state_backs / trim_astbar_grid_dim were
 * static inline; they are now plain external functions so the render TU can
 * call them. */
void print_astbar_tiles (ASTBarData * tbar);
void flush_tbar_state_backs (ASTBarData * tbar, int state);
int trim_astbar_grid_dim (short *dim, int size, int space_left);

#endif /* DECOR_INTERNAL_H */
