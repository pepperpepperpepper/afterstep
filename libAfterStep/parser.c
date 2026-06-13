/*
 * Copyright (c) 2000 Andrew Ferguson <andrew@owsla.cjb.net>
 * Copyright (c) 1998 Sasha Vasko <sasha at aftercode.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.   See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#undef LOCAL_DEBUG
#undef DO_CLOCKING
#undef UNKNOWN_KEYWORD_WARNING

#include "../configure.h"

#include <fcntl.h>
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <unistd.h>

#ifdef DO_CLOCKING
#if TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif
#endif
#include "asapp.h"
#include "afterstep.h"
#include "parser.h"
#include "freestor.h"
#include "functions.h"
#include "../libAfterBase/xml.h"
#include "parser_internal.h"

char *_disabled_keyword = DISABLED_KEYWORD;
char *_unknown_keyword = "unknown";

static ASHashTable *Keyword2IDHash = NULL;


void register_keyword_id (const char *keyword, int id)
{
	ASHashData hdata = { 0 };

	if (Keyword2IDHash == NULL)
		Keyword2IDHash = create_ashash (0, NULL, NULL, NULL);

	hdata.cptr = (char *)keyword;
	add_hash_item (Keyword2IDHash, AS_HASHABLE (id), hdata.vptr);
}

const char *keyword_id2keyword (int id)
{
	ASHashData hdata = { 0 };

	if (Keyword2IDHash == NULL)
		return _unknown_keyword;

	if (get_hash_item (Keyword2IDHash, AS_HASHABLE (id), &hdata.vptr) ==
			ASH_Success)
		return hdata.cptr;

	return _unknown_keyword;
}


void flush_keyword_ids ()
{
	if (Keyword2IDHash != NULL)
		destroy_ashash (&Keyword2IDHash);
}

void BuildHash (SyntaxDef * syntax)
{
	register int i;

	LOCAL_DEBUG_CALLER_OUT ("syntax = \"%s\"", syntax->display_name);

	if (syntax->term_hash_size <= 0)
		syntax->term_hash_size = TERM_HASH_SIZE;
	if (syntax->term_hash == NULL) {
		syntax->term_hash =
				create_ashash (syntax->term_hash_size, option_hash_value,
											 option_compare, NULL);
		LOCAL_DEBUG_OUT ("created hash %p", syntax->term_hash);
	}
	LOCAL_DEBUG_OUT ("adding hash entries ... %s", "");
	for (i = 0; syntax->terms[i].keyword; i++) {
		add_hash_item (syntax->term_hash,
									 AS_HASHABLE (syntax->terms[i].keyword),
									 (void *)&(syntax->terms[i]));
		register_keyword_id (syntax->terms[i].keyword, syntax->terms[i].id);
	}
	LOCAL_DEBUG_OUT ("%d hash entries added.", i);
}

void PrepareSyntax (SyntaxDef * syntax)
{
	if (syntax) {
		register int i;

		LOCAL_DEBUG_OUT ("syntax = \"%s\", recursion = %d",
										 syntax->display_name, syntax->recursion);
		if (syntax->recursion > 0)
			return;
		syntax->recursion++;

		if (syntax->term_hash == NULL)
			BuildHash (syntax);

		for (i = 0; syntax->terms[i].keyword; i++)
			if (syntax->terms[i].sub_syntax)
				PrepareSyntax (syntax->terms[i].sub_syntax);

		syntax->recursion--;
	}
}

void FreeSyntaxHash (SyntaxDef * syntax)
{
	if (syntax) {
		register int i;

		LOCAL_DEBUG_CALLER_OUT ("syntax = \"%s\", recursion = %d",
														syntax->display_name, syntax->recursion);

		if (syntax->recursion > 0)
			return;
		syntax->recursion++;

		if (syntax->term_hash)
			destroy_ashash (&(syntax->term_hash));

		for (i = 0; syntax->terms[i].keyword; i++)
			if (syntax->terms[i].sub_syntax)
				/* this should prevent us from endless recursion */
				if (syntax->terms[i].sub_syntax->term_hash != NULL)
					FreeSyntaxHash (syntax->terms[i].sub_syntax);

		syntax->recursion--;

	}
}

/* Syntax and storage stack operations */
void PushSyntax (ConfigDef * config, SyntaxDef * syntax)
{
	SyntaxStack *pnew = (SyntaxStack *) safecalloc (1, sizeof (SyntaxStack));

	/* saving our status to be able to come back to it later */
	if (config->current_syntax) {
		config->current_syntax->current_term = config->current_term;
		config->current_syntax->current_flags = config->current_flags;
	}
	config->current_flags = CF_NONE;
	config->current_term = NULL;
	pnew->next = config->current_syntax;
	pnew->syntax = syntax;
	if (config->syntax && syntax->terminator == '\n') {	/* handling prepending */
		SyntaxDef *csyntax = config->syntax;
		int len = strlen (csyntax->prepend_sub);
		register int i;
		char *tmp;

		if (config->current_prepend_allocated - config->current_prepend_size <=
				len) {
			config->current_prepend_allocated += len + 1;
			tmp = safemalloc (config->current_prepend_allocated);
			if (config->current_prepend) {
				strcpy (tmp, config->current_prepend);
				free (config->current_prepend);
			}
			config->current_prepend = tmp;
		}
		tmp = &(config->current_prepend[config->current_prepend_size]);

		for (i = 0; i <= len; i++)
			tmp[i] = csyntax->prepend_sub[i];

		config->current_prepend_size += len;

	}
	LOCAL_DEBUG_OUT ("%p: \"%s\", old is %p:  current_prepend = [%s]\n",
									 syntax, syntax->display_name, config->syntax,
									 config->current_prepend);
	config->current_syntax = pnew;
	config->syntax = syntax;
}

int PopSyntax (ConfigDef * config)
{
	if (config->current_syntax->next) {
		SyntaxStack *pold = config->current_syntax;

		config->current_syntax = config->current_syntax->next;
		config->syntax = config->current_syntax->syntax;

		/* restoring our status */
		config->current_term = config->current_syntax->current_term;
		config->current_flags = config->current_syntax->current_flags;

		LOCAL_DEBUG_OUT ("%p: \"%s\", old is %p: term = \"%s\"\n",
										 config->syntax, config->syntax->display_name,
										 pold->syntax,
										 config->current_term ? config->
										 current_term->keyword : "");

		if (pold->syntax->terminator == '\n') {
			if ((config->current_prepend_size -=
					 strlen (config->syntax->prepend_sub)) >= 0)
				config->current_prepend[config->current_prepend_size] = '\0';
		}
/*fprintf( stderr, "PopSyntax(%s) current_prepend = [%s]\n", pold->syntax->display_name, config->current_prepend );
*/
		free (pold);
		return 1;
	}
	return 0;
}

void PushStorage (ConfigDef * config, void *storage)
{
	StorageStack *pnew =
			(StorageStack *) safecalloc (1, sizeof (StorageStack));

	pnew->storage = storage;
	pnew->next = config->current_tail;
	config->current_tail = pnew;
}

int PopStorage (ConfigDef * config)
{
	if (config->current_tail)
		if (config->current_tail->next) {
			StorageStack *pold = config->current_tail;

			config->current_tail = config->current_tail->next;
			free (pold);
			return 1;
		}
	return 0;
}


void config_error (ConfigDef * config, char *err_text)
{
	if (config) {
		char *eol = strchr (config->tline, '\n');

		if (eol)
			*eol = '\0';
		show_error ("in %s (line %d):%s[%.50s]",
								config->current_syntax->syntax->display_name,
								config->line_count, err_text, config->tline);
		if (eol)
			*eol = '\n';
	}
}

/* Creating and Initializing new ConfigDef */
ConfigDef *NewConfig (char *myname, SyntaxDef * syntax,
											ConfigDataType type, ConfigData source,
											SpecialFunc special, int create)
{
	ConfigDef *new_conf;

	if (myname == NULL)
		return NULL;

	new_conf = (ConfigDef *) safecalloc (1, sizeof (ConfigDef));
	new_conf->special = special;
	new_conf->fd = -1;
	new_conf->fp = NULL;
	new_conf->flags = 0;
	if (source.vptr != NULL)
		switch (type) {
		case CDT_Filename:
			{
				char *realfilename = put_file_home (source.filename);

				if (!realfilename) {
					free (new_conf);
					return NULL;
				}
				new_conf->fd =
#ifdef __CYGWIN__
						open (realfilename,
									create ? O_CREAT | O_RDONLY | O_BINARY : O_RDONLY,
									S_IRUSR | S_IWUSR | S_IRGRP | O_BINARY);
#else
						open (realfilename, create ? O_CREAT | O_RDONLY : O_RDONLY,
									S_IRUSR | S_IWUSR | S_IRGRP);
#endif
				free (realfilename);
				set_flags (new_conf->flags, CP_NeedToCloseFile);
			}
			break;
		case CDT_FilePtr:
			new_conf->fp = source.fileptr;
			new_conf->fd = fileno (new_conf->fp);
			break;
		case CDT_FileDesc:
			new_conf->fd = *source.filedesc;
			break;
		case CDT_Data:
			break;
		case CDT_FilePtrAndData:
			new_conf->fp = source.fileptranddata->fp;
			new_conf->fd = fileno (new_conf->fp);
			set_flags (new_conf->flags, CP_ReadLines);
			break;

		}

	if (new_conf->fd != -1 && new_conf->fp == NULL) {
		new_conf->fp =
				fdopen (new_conf->fd,
								get_flags (new_conf->flags, CP_ReadLines) ? "rt" : "rb");
		set_flags (new_conf->flags, CP_NeedToFCloseFile);
	}


	new_conf->myname = mystrdup (myname);
	new_conf->current_syntax = NULL;
	new_conf->current_tail = NULL;
	new_conf->current_prepend = NULL;
	new_conf->current_prepend_size = 0;
	new_conf->current_prepend_allocated = 0;

	PushSyntax (new_conf, syntax);

	PrepareSyntax (syntax);

	/* allocated to store lines read from the file */
	new_conf->buffer = NULL;
	new_conf->buffer_size = 0;
	new_conf->bytes_in = 0;

	/* this is the current parsing information */
	new_conf->tline = new_conf->tdata = new_conf->tline_start = NULL;
	new_conf->current_term = NULL;
	new_conf->current_data_size = MAXLINELENGTH + 1;
	new_conf->current_data =
			(char *)safemalloc (new_conf->current_data_size);
	new_conf->current_data_len = 0;
	new_conf->cursor = NULL;
	return new_conf;
}

/* reader initialization */
ConfigDef *InitConfigReader (char *myname, SyntaxDef * syntax,
														 ConfigDataType type, ConfigData source,
														 SpecialFunc special)
{
	ConfigDef *new_conf =
			NewConfig (myname, syntax, type, source, special, False);

	if (new_conf == NULL)
		return NULL;
	if (source.vptr == NULL) {
		DestroyConfig (new_conf);
		return NULL;
	}

	if (type == CDT_Data) {
		/* allocate to store entire data */
		new_conf->buffer_size = strlen (source.data) + 1;
		new_conf->buffer = (char *)safemalloc (new_conf->buffer_size);
		strcpy (new_conf->buffer, source.data);
		new_conf->bytes_in = new_conf->buffer_size - 1;
	} else if (type == CDT_FilePtrAndData) {
		FilePtrAndData *fpd = source.fileptranddata;
		int buf_len = fpd->data ? strlen (fpd->data) : 0;

		if (buf_len < MAXLINELENGTH)
			buf_len = MAXLINELENGTH;
		new_conf->buffer_size = buf_len;
		new_conf->buffer = (char *)safemalloc (buf_len + 1);
		if (fpd->data)
			strcpy (new_conf->buffer, fpd->data);
		else
			new_conf->buffer[0] = '\0';
	} else {
		new_conf->buffer_size = MAXLINELENGTH + 1;
		new_conf->buffer = (char *)safemalloc (new_conf->buffer_size);
		new_conf->buffer[0] = '\0';
	}

	/* this is the current parsing information */
	new_conf->cursor = &(new_conf->buffer[0]);
	new_conf->line_count = 1;

	return new_conf;
}

/* debugging stuff */
#ifdef DEBUG_PARSER
void PrintSyntax (SyntaxDef * syntax)
{
	if (syntax->recursion > 0)
		return;
	syntax->recursion++;

	fprintf (stderr, "\nSentence Terminator: [0x%2.2x]", syntax->terminator);
	fprintf (stderr, "\nConfig Terminator:   [0x%2.2x]",
					 syntax->file_terminator);
	syntax->recursion--;
}


void PrintConfigReader (ConfigDef * config)
{
	PrintSyntax (config->syntax);
}

#endif


/* reader de-initialization */
void DestroyConfig (ConfigDef * config)
{
	free (config->myname);
	if (config->buffer)
		free (config->buffer);
	if (config->current_data)
		free (config->current_data);
	while (PopSyntax (config)) ;
	if (config->current_syntax)
		free (config->current_syntax);
	while (PopStorage (config)) ;
	if (config->current_prepend)
		free (config->current_prepend);
	if (config->current_tail)
		free (config->current_tail);
	if (config->syntax)
		FreeSyntaxHash (config->syntax);
	if (get_flags (config->flags, CP_NeedToCloseFile) && config->fd != -1)
		close (config->fd);
	if (get_flags (config->flags, CP_NeedToFCloseFile) && config->fp != NULL)
		fclose (config->fp);
	free (config);
}

