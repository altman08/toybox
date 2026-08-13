/* lslocks.c - list local system locks
 *
 * Copyright 2024 The Toybox Authors
 *
 * See https://www.kernel.org/doc/html/latest/filesystems/proc.html
 * /proc/locks format:
 *   id: type  mandatory  mode  pid  maj:min:inode  start  end

USE_LSLOCKS(NEWTOY(lslocks, "n", TOYFLAG_USR|TOYFLAG_BIN))

config LSLOCKS
  bool "lslocks"
  default n
  help
    usage: lslocks [-n]

    List local system locks.

    -n		Don't resolve pids to process names
*/

#define FOR_lslocks
#include "toys.h"

GLOBALS(
)

// One entry in the pid-info cache: maps dev:inode to tgid+cmdname+path.
// Built by scanning /proc/[tgid]/fdinfo/[fd] for "lock:" lines.
struct pid_info {
  struct pid_info *next;
  dev_t dev;
  ino_t inode;
  int tgid;
  char cmdname[64];
  char path[256];
};

// Walk /proc/[tgid]/fd/ to find the path of the file with the given inode,
// and fill pi->path. Returns 1 on success.
static int fill_path(struct pid_info *pi)
{
  DIR *dir;
  struct dirent *de;
  char *fddir = xmprintf("/proc/%d/fd", pi->tgid);
  int found = 0;

  if (!(dir = opendir(fddir))) { free(fddir); return 0; }
  while ((de = readdir(dir))) {
    struct stat st;
    char *fdpath;

    if (!isdigit(de->d_name[0])) continue;
    fdpath = xmprintf("/proc/%d/fd/%s", pi->tgid, de->d_name);
    if (!stat(fdpath, &st) && st.st_dev == pi->dev && st.st_ino == pi->inode) {
      readlink0(fdpath, pi->path, sizeof(pi->path));
      free(fdpath);
      found = 1;
      break;
    }
    free(fdpath);
  }
  closedir(dir);
  free(fddir);
  return found;
}

// Scan /proc/[tgid]/fdinfo/ for "lock:" lines.
// For each lock line, parse dev:inode and insert into the cache list.
static void scan_fdinfo(struct pid_info **cache, int tgid, const char *cmdname)
{
  DIR *dir;
  struct dirent *de;
  char *fdinfo = xmprintf("/proc/%d/fdinfo", tgid);

  if (!(dir = opendir(fdinfo))) { free(fdinfo); return; }
  while ((de = readdir(dir))) {
    FILE *fp;
    char *line;
    char *path;

    if (!isdigit(de->d_name[0])) continue;
    path = xmprintf("/proc/%d/fdinfo/%s", tgid, de->d_name);
    if (!(fp = fopen(path, "r"))) { free(path); continue; }
    free(path);
    while ((line = xgetline(fp))) {
      // "lock:\t<id>: <type> <mand> <mode> <pid> <MAJ:MIN:INODE> <start> <end>"
      char *p = line;

      if (strstart(&p, "lock:\t")) {
        unsigned maj, min, dummy_pid;
        unsigned long long inode;
        char devstr[32];
        struct pid_info *pi;

        // parse: id: type mand mode pid MAJ:MIN:INODE ...
        if (sscanf(p, "%*d: %*s %*s %*s %u %31s", &dummy_pid, devstr) == 2
            && sscanf(devstr, "%x:%x:%llu", &maj, &min, &inode) == 3) {
          pi = xzalloc(sizeof(*pi));
          pi->dev   = dev_makedev(maj, min);
          pi->inode = (ino_t)inode;
          pi->tgid  = tgid;
          strncpy(pi->cmdname, cmdname, sizeof(pi->cmdname)-1);
          fill_path(pi);
          pi->next  = *cache;
          *cache    = pi;
        }
      }
      free(line);
    }
    fclose(fp);
  }
  closedir(dir);
  free(fdinfo);
}

// Build the cache by iterating all /proc/[0-9]* entries.
static struct pid_info *build_pid_cache(void)
{
  DIR *procdir;
  struct dirent *de;
  struct pid_info *cache = NULL;

  if (!(procdir = opendir("/proc"))) return NULL;
  while ((de = readdir(procdir))) {
    int tgid;
    char cmdname[64];
    char *commpath;

    if (!isdigit(de->d_name[0])) continue;
    tgid = atoi(de->d_name);
    commpath = xmprintf("/proc/%d/comm", tgid);
    if (readfile(commpath, cmdname, sizeof(cmdname) - 1)) {
      chomp(cmdname);
      scan_fdinfo(&cache, tgid, cmdname);
    }
    free(commpath);
  }
  closedir(procdir);
  return cache;
}

// Look up dev:inode in the cache. Returns matching entry or NULL.
static struct pid_info *cache_lookup(struct pid_info *cache,
                                     dev_t dev, ino_t inode)
{
  for (; cache; cache = cache->next)
    if (cache->dev == dev && cache->inode == inode) return cache;
  return NULL;
}

static void free_pid_cache(struct pid_info *cache)
{
  while (cache) {
    struct pid_info *next = cache->next;
    free(cache);
    cache = next;
  }
}

// Fallback path: use xgetmountlist() to find the mountpoint for a device,
// then format as "mountpoint[inode]".
static void mountinfo_path(dev_t dev, unsigned long long inode,
                            char *buf, int buflen)
{
  // xgetmountlist(NULL) reads /proc/mounts and fills mt->stat via stat()
  struct mtab_list *ml, *mtlist = xgetmountlist(NULL);

  for (ml = mtlist; ml; ml = ml->next) {
    if (ml->stat.st_dev == dev) {
      snprintf(buf, buflen, "%.*s[%llu]", buflen - 24, ml->dir, inode);
      llist_traverse(mtlist, free);
      return;
    }
  }
  llist_traverse(mtlist, free);
  snprintf(buf, buflen, "%u:%u[%llu]",
           dev_major(dev), dev_minor(dev), inode);
}

// Get file size via stat on the resolved path; return -1 if unavailable
static long long get_filesize(const char *path)
{
  struct stat st;

  if (!path || !*path || path[0] != '/') return -1;
  if (stat(path, &st)) return -1;
  return (long long)st.st_size;
}

// One parsed lock row (used for two-pass column-width calculation)
struct lock_row {
  struct lock_row *next;
  char cmd[64];    // COMMAND or PID string
  char pid[16];
  char type[16];
  char size[16];
  char mode[16];
  char m[4];       // "0" or "1"
  char start[32];
  char end[32];
  char path[256];
};

// Column indices
#define COL_CMD   0
#define COL_PID   1
#define COL_TYPE  2
#define COL_SIZE  3
#define COL_MODE  4
#define COL_M     5
#define COL_START 6
#define COL_END   7
#define COL_PATH  8
#define NCOLS     9

void lslocks_main(void)
{
  FILE *fp;
  char *line;
  int id, pid;
  struct pid_info *cache;
  struct lock_row *rows = NULL, *row, **tail = &rows;
  int w[NCOLS], i;
  // header strings: with-command and no-command (-n) modes
  const char *hdr_cmd[NCOLS]  = {"COMMAND","PID","TYPE","SIZE","MODE","M","START","END","PATH"};
  const char *hdr_n[NCOLS]    = {"PID",    "",   "TYPE","SIZE","MODE","M","START","END","PATH"};

  // Always build cache: needed for path resolution even with -n
  cache = build_pid_cache();

  fp = xfopen("/proc/locks", "r");

  // ---- Pass 1: parse all rows, accumulate column widths ----
  // Initialise widths from header
  for (i = 0; i < NCOLS; i++)
    w[i] = strlen(FLAG(n) ? hdr_n[i] : hdr_cmd[i]);

  while ((line = xgetline(fp))) {
    char locktype[16], mandatory[16], mode[16];
    char end_str[32], devstr[32];
    unsigned maj, min;
    unsigned long long inode;
    long long start, end;
    char cmdname[64];
    char filepath[256];
    char size_str[16];

    // Parse: id: [->] TYPE MANDATORY MODE PID MAJ:MIN:INODE START END
    {
      char tmp[8];
      int blocked = 0;

      if (sscanf(line, "%d: %7s", &id, tmp) == 2 && !strcmp(tmp, "->"))
        blocked = 1;

      if (blocked) {
        if (sscanf(line, "%d: -> %15s %15s %15s %d %31s %lld %31s",
                   &id, locktype, mandatory, mode, &pid, devstr,
                   &start, end_str) != 8) { free(line); continue; }
      } else {
        if (sscanf(line, "%d: %15s %15s %15s %d %31s %lld %31s",
                   &id, locktype, mandatory, mode, &pid, devstr,
                   &start, end_str) != 8) { free(line); continue; }
      }
    }

    if (sscanf(devstr, "%x:%x:%llu", &maj, &min, &inode) != 3)
      { free(line); continue; }

    free(line);

    if (!strcmp(end_str, "EOF")) end = -1;
    else end = atoll(end_str);

    dev_t dev = dev_makedev(maj, min);
    filepath[0] = '\0';

    if (pid <= 0) {
      xstrncpy(cmdname, "(unknown)", sizeof(cmdname));
    } else {
      struct pid_info *pi = cache_lookup(cache, dev, (ino_t)inode);
      if (pi) {
        xstrncpy(filepath, pi->path, sizeof(filepath));
        if (FLAG(n)) snprintf(cmdname, sizeof(cmdname), "%d", pid);
        else xstrncpy(cmdname, pi->cmdname, sizeof(cmdname));
      } else {
        if (FLAG(n)) snprintf(cmdname, sizeof(cmdname), "%d", pid);
        else {
          char *commpath = xmprintf("/proc/%d/comm", pid);

          if (readfile(commpath, cmdname, sizeof(cmdname) - 1)) chomp(cmdname);
          else snprintf(cmdname, sizeof(cmdname), "%d", pid);
          free(commpath);
        }
      }
    }

    if (!filepath[0]) {
      if (pid > 0) {
        DIR *dir;
        struct dirent *de;
        char *fddir = xmprintf("/proc/%d/fd", pid);

        if ((dir = opendir(fddir))) {
          while ((de = readdir(dir))) {
            struct stat st;
            char *fdpath;

            if (!isdigit(de->d_name[0])) continue;
            fdpath = xmprintf("/proc/%d/fd/%s", pid, de->d_name);
            if (!stat(fdpath, &st) && st.st_dev == dev
                && st.st_ino == (ino_t)inode) {
              readlink0(fdpath, filepath, sizeof(filepath));
              free(fdpath);
              break;
            }
            free(fdpath);
          }
          closedir(dir);
        }
        free(fddir);
      }
      if (!filepath[0])
        mountinfo_path(dev, inode, filepath, sizeof(filepath));
    }

    {
      long long sz = get_filesize(filepath);

      if (sz < 0) xstrncpy(size_str, "?", sizeof(size_str));
      else human_readable(size_str, (unsigned long long)sz, HR_B);
    }

    // Build row
    row = xzalloc(sizeof(*row));
    if (FLAG(n)) snprintf(row->cmd, sizeof(row->cmd), "%d", pid);
    else xstrncpy(row->cmd, cmdname, sizeof(row->cmd));
    snprintf(row->pid,   sizeof(row->pid),   "%d",   pid);
    xstrncpy(row->type, locktype, sizeof(row->type));
    xstrncpy(row->size, size_str, sizeof(row->size));
    xstrncpy(row->mode, mode,     sizeof(row->mode));
    snprintf(row->m,    sizeof(row->m),    "%d",   !strcmp(mandatory,"MANDATORY"));
    snprintf(row->start,sizeof(row->start),"%lld",  start);
    if (end == -1) xstrncpy(row->end, "0", sizeof(row->end));
    else snprintf(row->end, sizeof(row->end), "%lld", end);
    xstrncpy(row->path, filepath, sizeof(row->path));

    // Measure column widths
    w[COL_CMD]   = maxof(w[COL_CMD],   (int)strlen(row->cmd));
    w[COL_PID]   = maxof(w[COL_PID],   (int)strlen(row->pid));
    w[COL_TYPE]  = maxof(w[COL_TYPE],  (int)strlen(row->type));
    w[COL_SIZE]  = maxof(w[COL_SIZE],  (int)strlen(row->size));
    w[COL_MODE]  = maxof(w[COL_MODE],  (int)strlen(row->mode));
    w[COL_M]     = maxof(w[COL_M],     (int)strlen(row->m));
    w[COL_START] = maxof(w[COL_START], (int)strlen(row->start));
    w[COL_END]   = maxof(w[COL_END],   (int)strlen(row->end));
    // PATH column: not padded (last column), no width needed

    *tail = row;
    tail  = &row->next;
  }
  fclose(fp);

  // ---- Pass 2: print header then rows ----
#define PFMT "%-*s %*s %*s %*s %-*s %*s %*s %*s %s\n"

  if (FLAG(n))
    printf(PFMT,
           w[COL_CMD],   hdr_n[COL_CMD],
           w[COL_PID],   "",
           w[COL_TYPE],  hdr_n[COL_TYPE],
           w[COL_SIZE],  hdr_n[COL_SIZE],
           w[COL_MODE],  hdr_n[COL_MODE],
           w[COL_M],     hdr_n[COL_M],
           w[COL_START], hdr_n[COL_START],
           w[COL_END],   hdr_n[COL_END],
           hdr_n[COL_PATH]);
  else
    printf(PFMT,
           w[COL_CMD],   hdr_cmd[COL_CMD],
           w[COL_PID],   hdr_cmd[COL_PID],
           w[COL_TYPE],  hdr_cmd[COL_TYPE],
           w[COL_SIZE],  hdr_cmd[COL_SIZE],
           w[COL_MODE],  hdr_cmd[COL_MODE],
           w[COL_M],     hdr_cmd[COL_M],
           w[COL_START], hdr_cmd[COL_START],
           w[COL_END],   hdr_cmd[COL_END],
           hdr_cmd[COL_PATH]);

  for (row = rows; row; row = row->next) {
    if (FLAG(n))
      printf(PFMT,
             w[COL_CMD],   row->cmd,
             w[COL_PID],   "",
             w[COL_TYPE],  row->type,
             w[COL_SIZE],  row->size,
             w[COL_MODE],  row->mode,
             w[COL_M],     row->m,
             w[COL_START], row->start,
             w[COL_END],   row->end,
             row->path);
    else
      printf(PFMT,
             w[COL_CMD],   row->cmd,
             w[COL_PID],   row->pid,
             w[COL_TYPE],  row->type,
             w[COL_SIZE],  row->size,
             w[COL_MODE],  row->mode,
             w[COL_M],     row->m,
             w[COL_START], row->start,
             w[COL_END],   row->end,
             row->path);
  }

#undef PFMT

  if (CFG_TOYBOX_FREE) {
    while (rows) { struct lock_row *next = rows->next; free(rows); rows = next; }
    free_pid_cache(cache);
  }
}
