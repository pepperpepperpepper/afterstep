#ifndef SESSION_INTERNAL_H
#define SESSION_INTERNAL_H

/* Shared between session.c and session_files.c. These helpers live in
 * session.c (with the desk-session and dir-tree code) but are also used
 * by the session file/path accessors in session_files.c. They are not
 * part of the public session.h API. */

char *check_file (const char *file);
ASDeskSession *get_desk_session (ASSession * session, int desk);

#endif /* SESSION_INTERNAL_H */
