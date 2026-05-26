#ifndef AFTERSTEP_PLACEMENT_INTERNAL_H
#define AFTERSTEP_PLACEMENT_INTERNAL_H

#include "asinternals.h"

/* Shared helpers for placement split translation units. */
ASVector *build_free_space_list (ASWindow *to_skip, ASGeometry *area,
																 int min_layer, int max_layer);

void apply_placement_result (ASStatusHints *status, XRectangle *anchor,
														 ASHints *hints, ASFlagType flags, int vx,
														 int vy, unsigned int width,
														 unsigned int height);

Bool find_closest_position (ASWindow *asw, ASGeometry *area,
														ASStatusHints *status, int *closest_x,
														int *closest_y, int max_layer);

Bool do_closest_placement (ASWindow *asw, ASWindowBox *aswbox,
													 ASGeometry *area);

Bool place_aswindow_in_windowbox (ASWindow *asw, ASWindowBox *aswbox,
																	ASUsePlacementStrategy which,
																	Bool force);

#endif /* AFTERSTEP_PLACEMENT_INTERNAL_H */

