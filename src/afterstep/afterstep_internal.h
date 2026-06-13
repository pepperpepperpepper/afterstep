#ifndef AFTERSTEP_INTERNAL_H
#define AFTERSTEP_INTERNAL_H

/* Shared private symbols for the afterstep.c <-> afterstep_screen.c split.
 * None of these are part of any public header.
 *
 * The session/display string globals are defined in afterstep.c
 * (next to main) but read by the screen-setup and session-lifecycle
 * helpers extracted into afterstep_screen.c. The forward declarations let the
 * extracted helpers call one another regardless of definition order (they
 * previously relied on the prototypes that lead afterstep.c).
 *
 * Types used below (SIGNAL_T, ScreenInfo, Bool) come from asinternals.h,
 * which every TU in this family includes before this header. */

/* file-local globals defined in afterstep.c */
extern char *GnomeSessionClientID;
extern char *original_DISPLAY_string;

/* extracted helpers defined in afterstep_screen.c */
void SetupScreen ();
void CleanupScreen ();
void IgnoreSignal (int sig);
SIGNAL_T Restart (int nonsense);
SIGNAL_T SigDone (int nonsense);
void CaptureAllWindows (ScreenInfo * scr);
void DoAutoexec (Bool restarting);
void RemapFunctions ();

#endif /* AFTERSTEP_INTERNAL_H */
