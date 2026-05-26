#ifndef AFTERSTEP_DECORATIONS_INTERNAL_H
#define AFTERSTEP_DECORATIONS_INTERNAL_H

#include "asinternals.h"

/* decorations_render.c */
ASCanvas *check_side_canvas (ASWindow * asw, FrameSide side, Bool required);
ASCanvas *check_frame_canvas (ASWindow * asw, Bool required);
ASCanvas *check_client_canvas (ASWindow * asw, Bool required);
ASCanvas *check_icon_canvas (ASWindow * asw, Bool required);
ASCanvas *check_icon_title_canvas (ASWindow * asw, Bool required,
													 Bool reuse_icon_canvas);

void geometry2slicing (ASGeometry g, int *pxs, int *pxe, int *pys, int *pye);

ASTBarData *check_tbar (ASTBarData ** tbar, Bool required,
													 const char *mystyle_name, ASImage * img,
													 unsigned short back_w,
													 unsigned short back_h, int flip,
													 ASFlagType align, ASFlagType fbevel,
													 ASFlagType ubevel, unsigned char fcm,
													 unsigned char ucm, int context,
													 ASGeometry * slicing);

#endif /* AFTERSTEP_DECORATIONS_INTERNAL_H */
