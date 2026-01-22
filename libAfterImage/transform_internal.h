#ifndef TRANSFORM_INTERNAL_H_INCLUDED
#define TRANSFORM_INTERNAL_H_INCLUDED

#include "transform.h"

extern ASVisual __transform_fake_asv;

static inline ASImage *
create_destination_image(unsigned int width,
						 unsigned int height,
						 ASAltImFormats format,
						 unsigned int compression,
						 ARGB32 back_color)
{
	ASImage *dst = create_asimage(width, height, compression);
	if (dst) {
		if (format != ASA_ASImage)
			set_flags(dst->flags, ASIM_DATA_NOT_USEFUL);

		dst->back_color = back_color;
	}
	return dst;
}

#endif /* TRANSFORM_INTERNAL_H_INCLUDED */
