#ifndef WMPROPS_INTERNAL_H
#define WMPROPS_INTERNAL_H

/* Shared between wmprops.c and wmprops_write.c. The prop_description_struct
 * type plus the handler hashes and description tables are defined in
 * wmprops.c (alongside the read handlers and the init code) and consumed by
 * flush_wmprop_data / handle_wmprop_event in wmprops_write.c. None of this
 * is part of the public wmprops.h API. */

typedef struct prop_description_struct {

	Atom *id_variable;
	 Bool (*read_func) (ASWMProps *, Bool deleted);
	WMPropClass prop_class;

#define WMP_NeedsCleanup    (0x01<<0)
#define WMP_ClientWritable  (0x01<<1)
	ASFlagType flags;
	Time updated_time;
} prop_description_struct;

/* Atom defined in wmprops.c but not exported via wmprops.h, needed by
 * the root-pixmap writer in wmprops_write.c. */
extern Atom ESETROOT_PMAP_ID;

extern ASHashTable *wmprop_root_handlers;
extern ASHashTable *wmprop_volitile_handlers;
extern prop_description_struct WMPropsDescriptions_root[];
extern prop_description_struct WMPropsDescriptions_volitile[];

#endif /* WMPROPS_INTERNAL_H */
