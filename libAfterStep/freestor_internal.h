#ifndef FREESTOR_INTERNAL_H
#define FREESTOR_INTERNAL_H

/* Shared across the freestor.c family (freestor.c / freestor_read.c /
 * freestor_write.c). None of these are part of the public freestor.h API.
 * The charkey_xref lookup type and the context/modifier tables live with
 * the decoding code in freestor_read.c but are also consumed by the
 * config-writing converters in freestor_write.c; FindTerm and
 * AddFreeStorageElem_sa live in freestor.c and are likewise used by the
 * writers. */

typedef struct charkey_xref {
	char key;
	int value;
} charkey_xref;

/* From freestor.c */
TermDef *FindTerm (SyntaxDef * syntax, int type, int id);
FreeStorageElem *AddFreeStorageElem_sa (SyntaxDef * syntax,
																				FreeStorageElem ** tail,
																				TermDef * pterm, int id,
																				char **strings, int count);

/* From freestor_read.c */
void context2string (char *string, int input, charkey_xref * table,
										 Bool terminate);
extern charkey_xref as_contexts[];
extern charkey_xref key_modifiers[];

#endif /* FREESTOR_INTERNAL_H */
