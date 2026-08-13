/* run_parts.c - Run scripts in a directory
 *
 * Copyright 2024 The Toybox Authors
 *
 * Ported from busybox run-parts (Bernhard Reutner-Fischer, Emanuele Aina,
 * based on Debian run-parts by Jeff Noxon and Guy Maor)
 *
 * See http://manpages.debian.org/run-parts

// run-parts has a hyphen in the name, which isn't a valid C identifier.
// Use the underscore name for NEWTOY (generates run_parts_main), then OLDTOY
// to expose the hyphenated alias that users actually invoke.
USE_RUN_PARTS(NEWTOY(run_parts, "^a*u:lter[!lt]", 0))
USE_RUN_PARTS(OLDTOY(run-parts, run_parts, TOYFLAG_BIN))

config RUN_PARTS
  bool "run-parts"
  default n
  help
    usage: run-parts [-a ARG]... [-u UMASK] [-l] [-t] [-e] [-r] DIRECTORY

    Run scripts or programs in DIRECTORY.

    Files are run in sorted order. Names must consist of ASCII letters,
    digits, underscores, hyphens, or non-leading dots to be considered.

    -a ARG  Pass ARG as argument to scripts (may be repeated)
    -e      Exit if a script returns non-zero
    -l      List matching files but don't run them
    -r      Reverse sort order
    -t      Test: print names but don't run (dry run)
    -u UMASK  Set umask before running scripts (default 022)
*/

#define FOR_run_parts
#include "toys.h"

GLOBALS(
  // NOTE: order must match option string right-to-left for args-bearing opts.
  // "^a*u:lter[!lt]" -> right-to-left: r,e,t,l have no arg; u: then a*
  // so u is this[0], a is this[1]
  char *u;
  struct arg_list *a;

  char **names;  // collected file names
  int count;     // how many collected
)

// Validate filename: must not start with '.', and contain only
// alnum, '-', '_', or non-leading '.'
static int valid_name(const char *path)
{
  const char *name = strrchr(path, '/');
  name = name ? name + 1 : path;

  // Leading dot is not allowed
  if (*name == '.') return 0;

  // Only allow: alnum, '_', '-', '.'
  for (; *name; name++)
    if (!isalnum(*name) && *name != '_' && *name != '-' && *name != '.')
      return 0;

  return 1;
}

// dirtree callback: collect valid executable (or listable) files
static int do_collect(struct dirtree *node)
{
  // Skip . and ..
  if (!dirtree_notdotdot(node)) return 0;

  // Only descend one level
  if (S_ISDIR(node->st.st_mode)) {
    // Descend into the top-level directory itself (no parent = top dir)
    if (!node->parent) return DIRTREE_RECURSE;
    // Don't recurse into subdirectories
    return 0;
  }

  // Must be regular file or symlink
  if (!S_ISREG(node->st.st_mode) && !S_ISLNK(node->st.st_mode)) return 0;

  // Validate the name
  if (!valid_name(node->name)) return 0;

  // In non-list mode, file must be executable
  if (!FLAG(l)) {
    if (faccessat(dirtree_parentfd(node), node->name, X_OK, 0)) return 0;
  }

  // Save full path
  char *path = dirtree_path(node, 0);
  TT.names = xrealloc(TT.names, (TT.count + 2) * sizeof(char *));
  TT.names[TT.count++] = path;
  TT.names[TT.count] = NULL;

  return 0;
}

// qsort comparator respecting -r flag
static int name_cmp(const void *a, const void *b)
{
  int r = strcmp(*(const char **)a, *(const char **)b);
  return FLAG(r) ? -r : r;
}

void run_parts_main(void)
{
  struct arg_list *al;
  int i, argc, ret, failed = 0;
  char **cmd;

  if (toys.optc != 1) help_exit("need exactly 1 argument");

  // Set umask
  if (TT.u) {
    char *end;
    unsigned long mask = strtoul(TT.u, &end, 8);
    if (*end || mask > 07777) error_exit("bad umask: %s", TT.u);
    umask((mode_t)mask);
  } else umask(022);

  // Collect files via dirtree
  dirtree_read(*toys.optargs, do_collect);

  if (!TT.names) return;

  // Sort
  qsort(TT.names, TT.count, sizeof(char *), name_cmp);

  // Count -a arguments
  argc = 0;
  for (al = TT.a; al; al = al->next) argc++;

  // Build cmd array: cmd[0]=name, cmd[1..argc]=-a args, cmd[argc+1]=NULL
  // toybox arg_list is in-order (appended at tail), so just copy directly
  cmd = xzalloc((argc + 2) * sizeof(char *));
  i = 1;
  for (al = TT.a; al; al = al->next) cmd[i++] = al->arg;
  cmd[i] = NULL;

  // Execute
  for (i = 0; i < TT.count; i++) {
    char *name = TT.names[i];

    if (FLAG(t) || FLAG(l)) {
      puts(name);
      continue;
    }

    cmd[0] = name;
    ret = xrun(cmd);
    if (ret == 0) continue;

    failed = 1;
    if (ret < 0)
      perror_msg("can't execute '%s'", name);
    else
      error_msg("%s: exit status %d", name, ret & 0xff);

    if (FLAG(e)) xexit();
  }

  if (CFG_TOYBOX_FREE) {
    for (i = 0; i < TT.count; i++) free(TT.names[i]);
    free(TT.names);
    free(cmd);
  }

  toys.exitval = failed;
}
