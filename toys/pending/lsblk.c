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

// One output row (for two-pass column-width calculation)
struct blkrow {
  struct blkrow *next;
  // iname: display name with optional tree-prefix (UTF-8 bytes)
  char iname[80];
  int  iname_dispw; // display width of iname (byte len minus 4 for each UTF-8 prefix)
  char majmin[32];  // "maj:min" string
  char rm[4];       // removable flag "0"/"1"
  char size[32];
  char ro[4];       // read-only flag "0"/"1"
  char type[16];
  char mnt[512];    // mountpoints (may be empty)
};

// Column indices
#define BC_NAME   0
#define BC_MAJMIN 1
#define BC_RM     2
#define BC_SIZE   3
#define BC_RO     4
#define BC_TYPE   5
#define BC_MNT    6
#define BNCOLS    7

// Collect one device into the row list and update column widths.
// is_last: 1=last child (└─), 0=not last (├─), -1=top-level (no prefix)
static void collect_dev(struct blkdev *bd, int is_last,
                        struct blkrow **tail, int w[])
{
  struct blkrow *r = xzalloc(sizeof(*r));
  char *mnt = get_mountpoints(bd->maj, bd->min);

  // Build display name with tree prefix
  if (is_last < 0) {
    snprintf(r->iname, sizeof(r->iname), "%s", bd->name);
    r->iname_dispw = strlen(bd->name);
  } else if (is_last) {
    // └─  = \xe2\x94\x94\xe2\x94\x80  (6 bytes, 2 display columns)
    snprintf(r->iname, sizeof(r->iname), "\xe2\x94\x94\xe2\x94\x80%s", bd->name);
    r->iname_dispw = 2 + strlen(bd->name);
  } else {
    // ├─  = \xe2\x94\x9c\xe2\x94\x80  (6 bytes, 2 display columns)
    snprintf(r->iname, sizeof(r->iname), "\xe2\x94\x9c\xe2\x94\x80%s", bd->name);
    r->iname_dispw = 2 + strlen(bd->name);
  }

  snprintf(r->majmin, sizeof(r->majmin), "%d:%d", bd->maj, bd->min);
  snprintf(r->rm,     sizeof(r->rm),     "%d",    bd->removable);
  fmt_size(r->size, bd->size);
  snprintf(r->ro,     sizeof(r->ro),     "%d",    bd->ro);
  strncpy(r->type, bd->type, sizeof(r->type) - 1);
  strncpy(r->mnt, mnt ? mnt : "", sizeof(r->mnt) - 1);
  free(mnt);

  // Update column widths (NAME column uses display width, not byte length)
  w[BC_NAME]   = maxof(w[BC_NAME],   r->iname_dispw);
  w[BC_MAJMIN] = maxof(w[BC_MAJMIN], (int)strlen(r->majmin));
  w[BC_RM]     = maxof(w[BC_RM],     (int)strlen(r->rm));
  w[BC_SIZE]   = maxof(w[BC_SIZE],   (int)strlen(r->size));
  w[BC_RO]     = maxof(w[BC_RO],     (int)strlen(r->ro));
  w[BC_TYPE]   = maxof(w[BC_TYPE],   (int)strlen(r->type));
  // MOUNTPOINTS is last column - no padding needed

  (*tail)->next = r;
  *tail = r;
}

void lsblk_main(void)
{
  DIR *sysblk, *devdir;
  struct dirent *de, *pde;
  struct double_list *disklist = NULL, *dl;

  // Sentinel head for the row list
  struct blkrow sentinel = {0}, *tail = &sentinel;
  int w[BNCOLS];
  const char *hdr[BNCOLS] = {"NAME","MAJ:MIN","RM","SIZE","RO","TYPE","MOUNTPOINTS"};
  int i;

  load_mounts();

  // Initialise widths from header strings
  for (i = 0; i < BNCOLS; i++) w[i] = strlen(hdr[i]);

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

  // ---- Pass 1: collect all rows, measure column widths ----
  for (dl = disklist; dl; dl = dl->next) {
    struct blkdev *bd = (struct blkdev *)dl->data;
    char devpath[128];
    struct double_list *parts = NULL, *pl;

    collect_dev(bd, -1, &tail, w);

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

    for (pl = parts; pl; pl = pl->next)
      collect_dev((struct blkdev *)pl->data,
                  pl->next == NULL ? 1 : 0, &tail, w);

    if (CFG_TOYBOX_FREE) {
      for (pl = parts; pl; pl = pl->next) free(pl->data);
      llist_traverse(parts, free);
    }
  }

  // ---- Pass 2: print header then rows ----
  // NAME column: iname is stored as UTF-8 bytes; for padding we need to
  // account for the 4 extra bytes (2 x 3-byte sequences vs 2 display cols).
  // We use %-*s with byte_width = display_width + extra_bytes_for_prefix.
#define PRINT_ROW(name_bytes, name_dispw) \
  printf("%-*s %*s %*s %*s %*s %-*s %s\n", \
         w[BC_NAME] + ((name_bytes) - (name_dispw)), (r->iname), \
         w[BC_MAJMIN], r->majmin, \
         w[BC_RM],     r->rm, \
         w[BC_SIZE],   r->size, \
         w[BC_RO],     r->ro, \
         w[BC_TYPE],   r->type, \
         r->mnt)

  // Print header (no UTF-8 prefix, dispw == bytelen)
  {
    struct blkrow hrow = {0};
    struct blkrow *r = &hrow;
    strncpy(hrow.iname, hdr[BC_NAME],   sizeof(hrow.iname)-1);
    strncpy(hrow.majmin,hdr[BC_MAJMIN], sizeof(hrow.majmin)-1);
    strncpy(hrow.rm,    hdr[BC_RM],     sizeof(hrow.rm)-1);
    strncpy(hrow.size,  hdr[BC_SIZE],   sizeof(hrow.size)-1);
    strncpy(hrow.ro,    hdr[BC_RO],     sizeof(hrow.ro)-1);
    strncpy(hrow.type,  hdr[BC_TYPE],   sizeof(hrow.type)-1);
    strncpy(hrow.mnt,   hdr[BC_MNT],    sizeof(hrow.mnt)-1);
    hrow.iname_dispw = strlen(hdr[BC_NAME]);
    PRINT_ROW(strlen(hrow.iname), hrow.iname_dispw);
  }

  {
    struct blkrow *r;
    for (r = sentinel.next; r; r = r->next)
      PRINT_ROW((int)strlen(r->iname), r->iname_dispw);
  }

#undef PRINT_ROW

  if (CFG_TOYBOX_FREE) {
    struct blkrow *r = sentinel.next;
    while (r) { struct blkrow *nx = r->next; free(r); r = nx; }
    for (dl = disklist; dl; dl = dl->next) free(dl->data);
    llist_traverse(disklist, free);
    llist_traverse(TT.mounts, free);
  }
}
