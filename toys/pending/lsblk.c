/* lsblk.c - list block devices
 *
 * Copyright 2024 The Toybox Authors
 *
 * Sources:
 *   /sys/block/          - block device list and attributes
 *   /proc/self/mountinfo - mount point information

USE_LSBLK(NEWTOY(lsblk, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config LSBLK
  bool "lsblk"
  default n
  help
    usage: lsblk

    List block devices.
*/

#define FOR_lsblk
#include "toys.h"

GLOBALS(
  struct double_list *mounts; // list of "devno dir" strings from mountinfo
)

struct blkdev {
  char name[64];           // kernel device name, e.g. "sda", "sda1"
  int maj, min;            // major:minor numbers
  unsigned long long size; // size in 512-byte sectors
  int ro;                  // read-only flag
  int removable;           // removable/hotplug flag
  char type[16];           // "disk", "part", "loop", "dm", etc.
  char parent[64];         // parent device name (for partitions, else empty)
};

// Read a sysfs file into buf (up to buflen-1 bytes), strip trailing newline.
// Returns 1 on success, 0 if file not readable.
static int sysfs_read(char *buf, int buflen, const char *fmt, ...)
{
  char path[256];
  va_list va;

  va_start(va, fmt);
  vsnprintf(path, sizeof(path), fmt, va);
  va_end(va);
  if (!readfile(path, buf, buflen - 1)) return 0;
  chomp(buf);
  return 1;
}

// Load /proc/self/mountinfo into TT.mounts as a list of strings "maj:min dir".
// We avoid xgetmountlist() because it calls stat() on every mountpoint which
// can be very slow in container environments with many bind-mounts.
static void load_mounts(void)
{
  FILE *fp = fopen("/proc/self/mountinfo", "r");
  char *line;

  if (!fp) return;
  while ((line = xgetline(fp))) {
    // mountinfo format: id parent_id maj:min root mountpoint ...
    int id, pid, maj, min;
    char root[256], mountpoint[256];

    if (sscanf(line, "%d %d %d:%d %255s %255s",
               &id, &pid, &maj, &min, root, mountpoint) == 6) {
      char *entry = xmprintf("%d:%d %s", maj, min, mountpoint);
      dlist_add(&TT.mounts, entry);
    }
    free(line);
  }
  fclose(fp);
  dlist_terminate(TT.mounts);
}

// Return a newly-allocated string with mountpoints for maj:min, space-joined.
// Returns NULL if not mounted.
static char *get_mountpoints(int maj, int min)
{
  struct double_list *dl;
  char prefix[32];
  char *result = NULL;

  snprintf(prefix, sizeof(prefix), "%d:%d ", maj, min);
  for (dl = TT.mounts; dl; dl = dl->next) {
    if (!strncmp(dl->data, prefix, strlen(prefix))) {
      const char *dir = dl->data + strlen(prefix);
      if (!result) result = xstrdup((char *)dir);
      else {
        char *tmp = xmprintf("%s %s", result, dir);
        free(result);
        result = tmp;
      }
    }
  }
  return result;
}

// Determine device type from sysfs.
static void get_devtype(struct blkdev *bd)
{
  char buf[64];

  // Check for partition attribute
  if (sysfs_read(buf, sizeof(buf),
      "/sys/block/%s/%s/partition", bd->parent[0] ? bd->parent : bd->name,
      bd->name)) {
    strcpy(bd->type, "part");
    return;
  }

  if (!strncmp(bd->name, "loop", 4)) { strcpy(bd->type, "loop"); return; }

  if (!strncmp(bd->name, "dm-", 3)) {
    if (sysfs_read(buf, sizeof(buf),
        "/sys/block/%s/dm/uuid", bd->name)) {
      char *dash = strchr(buf, '-');
      if (dash && dash != buf) {
        char *p;
        int len = dash - buf;
        if (len >= (int)sizeof(bd->type)) len = sizeof(bd->type) - 1;
        strncpy(bd->type, buf, len);
        bd->type[len] = '\0';
        for (p = bd->type; *p; p++) *p = tolower((unsigned char)*p);
        return;
      }
    }
    strcpy(bd->type, "dm");
    return;
  }

  if (!strncmp(bd->name, "md", 2)) {
    if (sysfs_read(buf, sizeof(buf), "/sys/block/%s/md/level", bd->name)) {
      strncpy(bd->type, buf, sizeof(bd->type) - 1);
      bd->type[sizeof(bd->type) - 1] = '\0';
      return;
    }
    strcpy(bd->type, "md");
    return;
  }

  strcpy(bd->type, "disk");
}

// Build a blkdev for a top-level device in /sys/block/<name>.
static struct blkdev *new_blkdev(const char *name)
{
  struct blkdev *bd = xzalloc(sizeof(*bd));
  char buf[64];

  strncpy(bd->name, name, sizeof(bd->name) - 1);

  if (!sysfs_read(buf, sizeof(buf), "/sys/block/%s/dev", name)) {
    free(bd); return NULL;
  }
  sscanf(buf, "%d:%d", &bd->maj, &bd->min);

  if (sysfs_read(buf, sizeof(buf), "/sys/block/%s/size", name)) {
    unsigned long long ull = strtoull(buf, NULL, 10);
    bd->size = ull;
  }
  if (sysfs_read(buf, sizeof(buf), "/sys/block/%s/ro", name))
    bd->ro = atoi(buf);
  if (sysfs_read(buf, sizeof(buf), "/sys/block/%s/removable", name))
    bd->removable = atoi(buf);

  get_devtype(bd);
  return bd;
}

// Build a blkdev for a partition /sys/block/<disk>/<part>.
static struct blkdev *new_partdev(const char *disk, const char *part,
                                  int disk_removable)
{
  struct blkdev *bd = xzalloc(sizeof(*bd));
  char buf[64];

  strncpy(bd->name, part, sizeof(bd->name) - 1);
  strncpy(bd->parent, disk, sizeof(bd->parent) - 1);
  strcpy(bd->type, "part");
  bd->removable = disk_removable;

  if (!sysfs_read(buf, sizeof(buf), "/sys/block/%s/%s/dev", disk, part)) {
    free(bd); return NULL;
  }
  sscanf(buf, "%d:%d", &bd->maj, &bd->min);

  if (sysfs_read(buf, sizeof(buf), "/sys/block/%s/%s/size", disk, part))
    bd->size = strtoull(buf, NULL, 10);
  if (sysfs_read(buf, sizeof(buf), "/sys/block/%s/%s/ro", disk, part))
    bd->ro = atoi(buf);

  return bd;
}

// Format byte count (from 512-byte sectors) as human-readable string (1024-based).
static void fmt_size(char *buf, unsigned long long sectors)
{
  human_readable(buf, sectors * 512, HR_B);
}

// Print one device row.
// is_last: 1=last child (use └─), 0=not last (use ├─), -1=top-level (no prefix)
static void print_dev(struct blkdev *bd, int is_last)
{
  char sizebuf[32], iname[80];
  char *mnt = get_mountpoints(bd->maj, bd->min);

  if (is_last < 0)
    snprintf(iname, sizeof(iname), "%s", bd->name);
  else if (is_last)
    snprintf(iname, sizeof(iname), "\xe2\x94\x94\xe2\x94\x80%s", bd->name); // └─
  else
    snprintf(iname, sizeof(iname), "\xe2\x94\x9c\xe2\x94\x80%s", bd->name); // ├─

  fmt_size(sizebuf, bd->size);

  // └─ and ├─ are each 2 UTF-8 chars = 6 bytes but display as 2 columns.
  // %-12s pads by byte count, so add 4 extra bytes for the 2 prefix chars.
  printf("%-*s %3d:%-4d %2d %6s %2d %-4s %s\n",
         is_last < 0 ? 12 : 16, iname,
         bd->maj, bd->min, bd->removable,
         sizebuf, bd->ro, bd->type, mnt ? mnt : "");
  free(mnt);
}

void lsblk_main(void)
{
  DIR *sysblk, *devdir;
  struct dirent *de, *pde;
  struct double_list *disklist = NULL, *dl;

  load_mounts();

  if (!(sysblk = opendir("/sys/block")))
    perror_exit("can't open /sys/block");

  // Build list of all top-level block devices
  while ((de = readdir(sysblk))) {
    struct blkdev *bd;
    if (de->d_name[0] == '.') continue;
    bd = new_blkdev(de->d_name);
    if (bd) dlist_add(&disklist, (char *)bd);
  }
  closedir(sysblk);

  // Sort by maj:min using a simple insertion sort (list is usually small)
  if (disklist) {
    struct double_list *sorted = NULL, *cur, *nxt;
    dlist_terminate(disklist);
    for (cur = disklist; cur; cur = nxt) {
      struct blkdev *a = (struct blkdev *)cur->data;
      struct double_list *ins, *prev = NULL;
      nxt = cur->next;
      for (ins = sorted; ins; ins = ins->next) {
        struct blkdev *b = (struct blkdev *)ins->data;
        if (a->maj < b->maj || (a->maj == b->maj && a->min < b->min)) break;
        prev = ins;
      }
      cur->prev = prev;
      cur->next = ins;
      if (prev) prev->next = cur; else sorted = cur;
      if (ins) ins->prev = cur;
    }
    dlist_terminate(sorted);
    disklist = sorted;
  }

  // Print header
  printf("%-12s %6s %2s %6s %2s %-4s %s\n",
         "NAME", "MAJ:MIN", "RM", "SIZE", "RO", "TYPE", "MOUNTPOINTS");

  // For each disk, print it then its partitions
  for (dl = disklist; dl; dl = dl->next) {
    struct blkdev *bd = (struct blkdev *)dl->data;
    char devpath[128];
    struct double_list *parts = NULL, *pl;

    print_dev(bd, -1);

    // Scan /sys/block/<name>/ for partition subdirs
    snprintf(devpath, sizeof(devpath), "/sys/block/%s", bd->name);
    if (!(devdir = opendir(devpath))) continue;
    while ((pde = readdir(devdir))) {
      struct blkdev *pd;
      char partfile[256];

      if (pde->d_name[0] == '.') continue;
      snprintf(partfile, sizeof(partfile), "/sys/block/%.60s/%.60s/partition",
               bd->name, pde->d_name);
      if (access(partfile, F_OK)) continue;

      pd = new_partdev(bd->name, pde->d_name, bd->removable);
      if (pd) dlist_add(&parts, (char *)pd);
    }
    closedir(devdir);

    // Sort partitions by minor number
    if (parts) {
      struct double_list *sorted = NULL, *cur, *nxt;
      dlist_terminate(parts);
      for (cur = parts; cur; cur = nxt) {
        struct blkdev *a = (struct blkdev *)cur->data;
        struct double_list *ins, *prev = NULL;
        nxt = cur->next;
        for (ins = sorted; ins; ins = ins->next) {
          struct blkdev *b = (struct blkdev *)ins->data;
          if (a->min < b->min) break;
          prev = ins;
        }
        cur->prev = prev;
        cur->next = ins;
        if (prev) prev->next = cur; else sorted = cur;
        if (ins) ins->prev = cur;
      }
      dlist_terminate(sorted);
      parts = sorted;
    }

    for (pl = parts; pl; pl = pl->next) {
      print_dev((struct blkdev *)pl->data, pl->next == NULL ? 1 : 0);
    }

    if (CFG_TOYBOX_FREE) {
      for (pl = parts; pl; pl = pl->next) free(pl->data);
      llist_traverse(parts, free);
    }
  }

  if (CFG_TOYBOX_FREE) {
    for (dl = disklist; dl; dl = dl->next) free(dl->data);
    llist_traverse(disklist, free);
    llist_traverse(TT.mounts, free);
  }
}
