#ifndef OPENTONY_PATH_SEPARATOR_H
#define OPENTONY_PATH_SEPARATOR_H

/* The separator a host path is joined with: the Windows build's own, '/' on Linux. (The game's own names keep their backslashes: the file layer turns
 * them into the host's.) */
#ifdef _WIN32
#define OT_PATH_SEPARATOR "\\"
#else
#define OT_PATH_SEPARATOR "/"
#endif

#endif /* OPENTONY_PATH_SEPARATOR_H */
