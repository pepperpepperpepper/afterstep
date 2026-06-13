#ifndef MOVERESIZE_INTERNAL_H
#define MOVERESIZE_INTERNAL_H

/* Grid snapping / size constraint helpers defined in
 * moveresize_constraints.c and called by move_func/resize_func in
 * moveresize.c. Not part of the public moveresize.h API. (The other
 * constraint helpers in moveresize_constraints.c are used only within
 * that TU and need no declaration here.) */

Bool attract_corner (ASMoveResizeData * data, int *x_inout, int *y_inout);
int adjust_west_side (ASGrid * grid, ASGridLine * gridlines, int dpos,
											int *pos_inout, int *size_inout, int lim1, int lim2,
											int title_west, int title_north);
int adjust_east_side (ASGrid * grid, ASGridLine * gridlines, int dpos,
											int pos, int *size_inout, int lim1, int lim2,
											int title_north);
int restrain_east_side (int dpos, int *size_inout, int min_val, int incr,
												int max_val);
int restrain_west_side (int dpos, int *wpos_inout, int *size_inout,
												int min_val, int incr, int max_val);

#endif /* MOVERESIZE_INTERNAL_H */
