/* gpiod.c - gpio tools
 *
 * Copyright 2021 The Android Open Source Project
 *
 * TODO: gpiomon

USE_GPIODETECT(NEWTOY(gpiodetect, ">0", TOYFLAG_USR|TOYFLAG_BIN))
USE_GPIOINFO(NEWTOY(gpioinfo, 0, TOYFLAG_USR|TOYFLAG_BIN))
USE_GPIOGET(NEWTOY(gpioget, "<2l", TOYFLAG_USR|TOYFLAG_BIN))
USE_GPIOFIND(NEWTOY(gpiofind, "<1>1", TOYFLAG_USR|TOYFLAG_BIN))
USE_GPIOSET(NEWTOY(gpioset, "<2l", TOYFLAG_USR|TOYFLAG_BIN))

config GPIODETECT
  bool "gpiodetect"
  default y
  help
    usage: gpiodetect

    Show all gpio chips' names, labels, and number of lines.

config GPIOFIND
  bool "gpiofind"
  default y
  help
    usage: gpiofind NAME

    Show the chip and line number for the given line name.

config GPIOINFO
  bool "gpioinfo"
  default y
  help
    usage: gpioinfo [CHIP...]

    Show gpio chips' lines.

config GPIOGET
  bool "gpioget"
  default y
  help
    usage: gpioget [-l] CHIP LINE...

    Gets the values of the given lines on CHIP. Use gpiofind to convert line
    names to numbers.

    -l	Active low

config GPIOSET
  bool "gpioset"
  default y
  help
    usage: gpioset [-l] CHIP LINE=VALUE...

    Set the lines on CHIP to the given values. Use gpiofind to convert line
    names to numbers.

    -l	Active low
*/

#define FOR_gpiodetect
#include "toys.h"

GLOBALS(
  struct double_list *chips;
  int chip_count;
)

// linux/gpio.h added in Linux 4.8 (commit 159f3cd9eca9)
#if __has_include(<linux/gpio.h>)
#include <linux/gpio.h>
#else
#define GPIO_MAX_NAME_SIZE 32
struct gpiochip_info {
  char name[GPIO_MAX_NAME_SIZE];
  char label[GPIO_MAX_NAME_SIZE];
  unsigned int lines;
};
#define GPIOLINE_FLAG_KERNEL      (1UL<<0)
#define GPIOLINE_FLAG_IS_OUT      (1UL<<1)
#define GPIOLINE_FLAG_ACTIVE_LOW  (1UL<<2)
struct gpioline_info {
  unsigned int line_offset;
  unsigned int flags;
  char name[GPIO_MAX_NAME_SIZE];
  char consumer[GPIO_MAX_NAME_SIZE];
};
#define GPIOHANDLES_MAX 64
#define GPIOHANDLE_REQUEST_INPUT       (1UL<<0)
#define GPIOHANDLE_REQUEST_OUTPUT      (1UL<<1)
#define GPIOHANDLE_REQUEST_ACTIVE_LOW  (1UL<<2)
struct gpiohandle_request {
  unsigned int lineoffsets[GPIOHANDLES_MAX];
  unsigned int flags;
  unsigned char default_values[GPIOHANDLES_MAX];
  char consumer_label[GPIO_MAX_NAME_SIZE];
  unsigned int lines;
  int fd;
};
struct gpiohandle_data {
  unsigned char values[GPIOHANDLES_MAX];
};
#define GPIO_GET_CHIPINFO_IOCTL     _IOR(0xB4, 0x01, struct gpiochip_info)
#define GPIO_GET_LINEINFO_IOCTL     _IOWR(0xB4, 0x02, struct gpioline_info)
#define GPIO_GET_LINEHANDLE_IOCTL   _IOWR(0xB4, 0x03, struct gpiohandle_request)
#define GPIOHANDLE_GET_LINE_VALUES_IOCTL _IOWR(0xB4, 0x08, struct gpiohandle_data)
#define GPIOHANDLE_SET_LINE_VALUES_IOCTL _IOWR(0xB4, 0x09, struct gpiohandle_data)
#endif

static int open_chip(char *chip)
{
  sprintf(toybuf, isdigit(*chip) ? "/dev/gpiochip%s" : "/dev/%s", chip);
  return xopen(toybuf, O_RDWR);
}

static int collect_chips(struct dirtree *node)
{
  int n;

  if (!node->parent) return DIRTREE_RECURSE; // Skip the directory itself.

  if (sscanf(node->name, "gpiochip%d", &n)!=1) return 0;

  dlist_add(&TT.chips, xstrdup(node->name));
  TT.chip_count++;

  return 0;
}

static int comparator(const void *a, const void *b)
{
  struct double_list *lhs = *(struct double_list **)a,
    *rhs = *(struct double_list **)b;

  return strcmp(lhs->data, rhs->data);
}

// call cb() in sorted order
static void foreach_chip(void (*cb)(char *name))
{
  struct double_list **sorted, *chip;
  int i = 0;

  dirtree_flagread("/dev", DIRTREE_SHUTUP, collect_chips);
  if (!TT.chips) return;

  sorted = xmalloc(TT.chip_count*sizeof(void *));
  for (chip = TT.chips; i<TT.chip_count; chip = chip->next) sorted[i++] = chip;
  qsort(sorted, TT.chip_count, sizeof(void *), comparator);

  for (i = 0; i<TT.chip_count; i++) {
    sprintf(toybuf, "/dev/%s", sorted[i]->data);
    cb(toybuf);
  }

  free(sorted);
  llist_traverse(TT.chips, llist_free_double);
}

static void gpiodetect(char *path)
{
  struct gpiochip_info chip;
  int fd = xopen(path, O_RDWR);

  xioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &chip);
  close(fd);

  // gpiochip0 [pinctrl-bcm2711] (58 line)
  printf("%s [%s] (%u line%s)\n", chip.name, chip.label, chip.lines,
         chip.lines==1?"":"s");
}

void gpiodetect_main(void)
{
  foreach_chip(gpiodetect);
}

#define FOR_gpiofind
#include "generated/flags.h"

static void gpiofind(char *path)
{
  struct gpiochip_info chip;
  struct gpioline_info line;
  int fd = xopen(path, O_RDWR);

  xioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &chip);

  for (line.line_offset=0; line.line_offset<chip.lines; line.line_offset++) {
    xioctl(fd, GPIO_GET_LINEINFO_IOCTL, &line);
    if (!strcmp(line.name, *toys.optargs)) {
      printf("%s %d\n", chip.name, line.line_offset);
      break;
    }
  }
  close(fd);
}

void gpiofind_main(void)
{
  foreach_chip(gpiofind);
}

#define FOR_gpioinfo
#include "generated/flags.h"

static void gpioinfo_fd(int fd)
{
  struct gpiochip_info chip;
  struct gpioline_info line;

  xioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &chip);

  // gpiochip1 - 8 lines:
  printf("%s - %d line%s:\n", chip.name, chip.lines, chip.lines==1?"":"s");

  //     line   4: "VDD_SD_IO_SEL" "vdd-sd-io" output active-high [used]
  // We use slightly wider columns for the name and consumer; just wide enough
  // to show all Raspberry Pi 400 pins without wrapping an 80-column terminal.
  for (line.line_offset=0; line.line_offset<chip.lines; line.line_offset++) {
    xioctl(fd, GPIO_GET_LINEINFO_IOCTL, &line);
    if (*line.name) sprintf(toybuf, "\"%s\"", line.name);
    else strcpy(toybuf, "unnamed");
    if (*line.consumer) sprintf(toybuf+64, "\"%s\"", line.consumer);
    else strcpy(toybuf+64, "unused");
    printf("\tline %3d:%18s %18s", line.line_offset, toybuf, toybuf+64);
    printf(" %sput", line.flags&GPIOLINE_FLAG_IS_OUT?"out":" in");
    printf(" active-%s", line.flags&GPIOLINE_FLAG_ACTIVE_LOW?"low ":"high");
    if (line.flags&GPIOLINE_FLAG_KERNEL) printf(" [used]");
    printf("\n");
  }

  close(fd);
}

static void gpioinfo(char *path)
{
  gpioinfo_fd(xopen(path, O_RDWR));
}

void gpioinfo_main(void)
{
  int i;

  if (!toys.optc) foreach_chip(gpioinfo);
  else for (i = 0; toys.optargs[i];i++) gpioinfo_fd(open_chip(toys.optargs[i]));
}

#define FOR_gpioget
#include "generated/flags.h"

// TODO: half the get/set plumbing same here, maybe collate?
void gpioget_main(void)
{
  struct gpiohandle_request req = { .flags = GPIOHANDLE_REQUEST_INPUT };
  struct gpiohandle_data data;
  struct gpiochip_info chip;
  char **args = toys.optargs;
  int fd, line;

  fd = open_chip(*args);
  xioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &chip);
  if (FLAG(l)) req.flags |= GPIOHANDLE_REQUEST_ACTIVE_LOW;
  for (args++; *args; args++, req.lines++) {
    if (req.lines >= GPIOHANDLES_MAX) error_exit("too many requests!");
    line = atolx_range(*args, 0, chip.lines);
    req.lineoffsets[req.lines] = line;
  }
  xioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req);
  xioctl(req.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data);
  for (line = 0; line<req.lines; line++)
    printf("%s%d", " "+(line<1), data.values[line]);
  xputc('\n');
}

#define FOR_gpioset
#include "generated/flags.h"

void gpioset_main(void)
{
  struct gpiohandle_request req = { .flags = GPIOHANDLE_REQUEST_OUTPUT };
  char **args = toys.optargs;
  int fd, value;

  fd = open_chip(*args);
  if (FLAG(l)) req.flags |= GPIOHANDLE_REQUEST_ACTIVE_LOW;
  for (args++; *args; args++, req.lines++) {
    if (req.lines == GPIOHANDLES_MAX) error_exit("too many requests!");
    if (sscanf(*args, "%d=%d", req.lineoffsets+req.lines, &value) != 2)
      perror_exit("not LINE=VALUE: %s", *args);
    req.default_values[req.lines] = value;
  }
  xioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req);
}
