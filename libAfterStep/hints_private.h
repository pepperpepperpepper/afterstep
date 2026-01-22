#ifndef HINTS_PRIVATE_H_INCLUDED
#define HINTS_PRIVATE_H_INCLUDED

/*
 * Internal (non-installed) helpers for splitting protocol-specific hint handling
 * out of libAfterStep/hints.c.
 *
 * NOTE: This header assumes AfterStep's internal includes are already in place
 * (i.e. include ../configure.h and asapp.h first) so types like Bool/ASFlagType
 * are available.
 */

#include "hints.h"

struct ASRawHints;
struct ASDatabaseRecord;

ASFlagType get_asdb_hint_mask (struct ASDatabaseRecord * db_rec);

/* Shared encode/decode helpers (implemented in hints_common.c). */
void decode_flags (ASFlagType * dst_flags, ASFlagsXref * xref,
									 ASFlagType set_flags, ASFlagType flags);
void encode_flags (ASFlagType * dst_flags, ASFlagsXref * xref,
									 ASFlagType set_flags, ASFlagType flags);

#define decode_simple_flags(dst,xref,flags) \
	decode_flags ((dst), (xref), ASFLAGS_EVERYTHING, (flags))
#define encode_simple_flags(dst,xref,flags) \
	encode_flags ((dst), (xref), ASFLAGS_EVERYTHING, (flags))

/* Name list helper (implemented in hints_common.c). */
int add_name_to_list (ASHints * hints, char *name, unsigned char encoding,
											Bool to_front);

/* Protocol-specific merge functions (implemented in hints_*.c). */
void merge_icccm_hints (ASHints * clean, struct ASRawHints * raw,
												struct ASDatabaseRecord * db_rec,
												ASStatusHints * status, ASFlagType what);
void merge_group_hints (ASHints * clean, struct ASRawHints * raw,
												struct ASDatabaseRecord * db_rec,
												ASStatusHints * status, ASFlagType what);
void merge_transient_hints (ASHints * clean, struct ASRawHints * raw,
														struct ASDatabaseRecord * db_rec,
														ASStatusHints * status, ASFlagType what);
void merge_motif_hints (ASHints * clean, struct ASRawHints * raw,
												struct ASDatabaseRecord * db_rec,
												ASStatusHints * status, ASFlagType what);
void merge_gnome_hints (ASHints * clean, struct ASRawHints * raw,
												struct ASDatabaseRecord * db_rec,
												ASStatusHints * status, ASFlagType what);
void merge_kde_hints (ASHints * clean, struct ASRawHints * raw,
											struct ASDatabaseRecord * db_rec,
											ASStatusHints * status, ASFlagType what);
void merge_extwm_hints (ASHints * clean, struct ASRawHints * raw,
												struct ASDatabaseRecord * db_rec,
												ASStatusHints * status, ASFlagType what);
void merge_xresources_hints (ASHints * clean, struct ASRawHints * raw,
														 struct ASDatabaseRecord * db_rec,
														 ASStatusHints * status, ASFlagType what);

/* Startup/command-line parsing (implemented in hints_xresources.c). */
void merge_command_line (ASHints * clean, ASStatusHints * status,
												 struct ASRawHints * raw);

/* Used by hints.c for property-driven updates. */
Bool decode_gnome_state (ASFlagType state, ASHints * clean,
												 ASStatusHints * status);

/* Protocol-specific "encode to properties" helpers (used by set_all_client_hints). */
Bool client_hints2motif_hints (MwmHints * motif_hints, ASHints * hints,
															 ASStatusHints * status);
Bool client_hints2gnome_hints (GnomeHints * gnome_hints, ASHints * hints,
															 ASStatusHints * status);
Bool client_hints2extwm_hints (ExtendedWMHints * extwm_hints, ASHints * hints,
															 ASStatusHints * status);

#endif /* HINTS_PRIVATE_H_INCLUDED */
