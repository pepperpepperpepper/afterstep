#ifndef ASAPP_INTERNAL_H
#define ASAPP_INTERNAL_H

/* Shared across the asapp.c family (asapp.c / asapp_env.c / asapp_spawn.c).
 * None of these are part of the public asapp.h API:
 *   AS_environ      - saved environment vector (set in asapp.c, read by
 *                     spawn_child in asapp_spawn.c)
 *   as_app_args     - parsed command-line args (read by InitSession in
 *                     asapp_env.c and by the child launcher in asapp_spawn.c)
 *   FuncSyntax      - function-config syntax (freed by free_func_hash in
 *                     asapp_env.c) */

extern char **AS_environ;
extern ASProgArgs as_app_args;
extern struct SyntaxDef FuncSyntax;

#endif /* ASAPP_INTERNAL_H */
