#undef DO_CLOCKING
#ifndef LOCAL_DEBUG
#define LOCAL_DEBUG
#endif
#ifndef EVENT_TRACE
#define EVENT_TRACE
#endif

#include "../../configure.h"
#include "../../libAfterStep/asapp.h"
#include "../../libAfterStep/session.h"
#include "../../libAfterStep/functions.h"
#include "../../libAfterStep/parser.h"
#include "../../libAfterConf/afterconf.h"

extern WharfConfig *Config;
extern int Rows_override;
extern int Columns_override;

void CheckConfigSanity ()
{
	if (Config == NULL)
		Config = CreateWharfConfig ();

	if (MyArgs.geometry.flags != 0)
		Config->geometry = MyArgs.geometry;

	if (Rows_override >= 0)
		Config->rows = Rows_override;
	if (Columns_override >= 0)
		Config->columns = Columns_override;

	if (Config->rows <= 0 && Config->columns <= 0)
		Config->rows = 1;

#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
	show_progress ("printing wharf config : ");
	PrintWharfConfig (Config);
#endif

}

void merge_wharf_folders (WharfButton ** pf1, WharfButton ** pf2)
{
	while (*pf1)
		pf1 = &((*pf1)->next);
	*pf1 = *pf2;
	*pf2 = NULL;
}

void RemapFunctions ()
{
	char *fname =
			make_session_data_file (Session, False, 0, AFTER_FUNC_REMAP, NULL);
	FeelConfig *feel_config = ParseFeelOptions (fname, MyName);

	free (fname);

	if (feel_config != NULL) {
		ComplexFunction *remap_func =
				find_complex_func (feel_config->feel->ComplexFunctions,
													 "RemapFunctions");
		if (remap_func) {
			int i;
			for (i = 0; i < remap_func->items_num; ++i)
				if (remap_func->items[i].func == F_Remap
						&& remap_func->items[i].name != NULL
						&& remap_func->items[i].text != NULL)
					change_func_code (remap_func->items[i].name,
														txt2func_code (remap_func->items[i].text));
		}
		DestroyFeelConfig (feel_config);
	}
}

void GetOptions (const char *filename)
{
	WharfConfig *config;
	WharfConfig *to = Config, *from;
	START_TIME (option_time);
	SHOW_CHECKPOINT;
	LOCAL_DEBUG_OUT ("loading wharf config from \"%s\": ", filename);
	from = config = ParseWharfOptions (filename, MyName);
	SHOW_TIME ("Config parsing", option_time);

	/* Need to merge new config with what we have already : */
	/* now lets check the config sanity : */
	/* mixing set and default flags : */
	Config->flags =
			(config->flags & config->set_flags) | (Config->
																						 flags & (~config->set_flags));
	Config->set_flags |= config->set_flags;

	if (get_flags (config->set_flags, WHARF_ROWS))
		Config->rows = config->rows;

	if (get_flags (config->set_flags, WHARF_COLUMNS))
		Config->columns = config->columns;

	if (get_flags (config->set_flags, WHARF_GEOMETRY))
		merge_geometry (&(config->geometry), &(Config->geometry));

	if (get_flags (config->set_flags, WHARF_WITHDRAW_STYLE))
		Config->withdraw_style = config->withdraw_style;

	if (get_flags (config->set_flags, WHARF_FORCE_SIZE))
		merge_geometry (&(config->force_size), &(Config->force_size));

	if (get_flags (config->set_flags, WHARF_ANIMATE_STEPS))
		Config->animate_steps = config->animate_steps;
	if (get_flags (config->set_flags, WHARF_ANIMATE_STEPS_MAIN))
		Config->animate_steps_main = config->animate_steps_main;
	if (get_flags (config->set_flags, WHARF_ANIMATE_DELAY))
		Config->animate_delay = config->animate_delay;
	ASCF_MERGE_SCALAR_KEYWORD (WHARF, to, from, LabelLocation);
	ASCF_MERGE_SCALAR_KEYWORD (WHARF, to, from, AlignContents);
	ASCF_MERGE_SCALAR_KEYWORD (WHARF, to, from, Bevel);
	ASCF_MERGE_SCALAR_KEYWORD (WHARF, to, from, ShowHints);
	ASCF_MERGE_SCALAR_KEYWORD (WHARF, to, from, CompositionMethod);
	ASCF_MERGE_SCALAR_KEYWORD (WHARF, to, from, FolderOffset);
	ASCF_MERGE_SCALAR_KEYWORD (WHARF, to, from, OrthogonalFolderOffset);

/*LOCAL_DEBUG_OUT( "align_contents = %d", Config->align_contents ); */
	if (get_flags (config->set_flags, WHARF_SOUND)) {
		int i;
		for (i = 0; i < WHEV_MAX_EVENTS; ++i) {
			set_string (&(Config->sounds[i]), mystrdup (config->sounds[i]));
			config->sounds[i] = NULL;
		}
	}
	/* merging folders : */

	if (config->root_folder)
		merge_wharf_folders (&(Config->root_folder), &(config->root_folder));

	if (Config->balloon_conf)
		Destroy_balloonConfig (Config->balloon_conf);
	Config->balloon_conf = config->balloon_conf;
	config->balloon_conf = NULL;

	if (config->style_defs)
		ProcessMyStyleDefinitions (&(config->style_defs));

	DestroyWharfConfig (config);
	FreeSyntaxHash (&WharfFolderSyntax);
	SHOW_TIME ("Config parsing", option_time);
}

void GetBaseOptions (const char *filename)
{
	START_TIME (started);

	ReloadASEnvironment (NULL, NULL, NULL, False, True);

	SHOW_TIME ("BaseConfigParsingTime", started);
}
