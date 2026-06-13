#ifndef CANVAS_INTERNAL_H
#define CANVAS_INTERNAL_H

/* Shared between canvas.c and canvas_x11.c. These shape helpers are
 * defined in canvas.c (alongside the canvas config code that also uses
 * them) and called from the X11 shape/display code in canvas_x11.c.
 * They are not part of the public canvas.h API. */

void set_canvas_shape_to_rectangle (ASCanvas * pc);
void set_canvas_shape_to_nothing (ASCanvas * pc);

#endif /* CANVAS_INTERNAL_H */
