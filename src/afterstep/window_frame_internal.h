#ifndef WINDOW_FRAME_INTERNAL_H
#define WINDOW_FRAME_INTERNAL_H

#include "asinternals.h"

/* From window_frame_layout.c */
void resize_canvases (ASWindow * asw, ASOrientation * od,
                      unsigned int normal_width, unsigned int normal_height,
                      unsigned int *frame_sizes);
int make_shade_animation_step (ASWindow * asw, ASOrientation * od);
ASFlagType resize_frame_subwindows (ASWindow * asw, ASOrientation * od,
                                    unsigned int frame_win_width,
                                    unsigned int frame_win_height);
Bool check_frame_side_config (ASWindow * asw, Window w, ASOrientation * od);
void move_shading_frame (ASWindow * asw, ASOrientation * od, int step_size);

/* From window_frame_render.c */
void update_window_frame_moved (ASWindow * asw, ASOrientation * od);
int update_window_tbar_size (ASWindow * asw);

#endif /* WINDOW_FRAME_INTERNAL_H */
