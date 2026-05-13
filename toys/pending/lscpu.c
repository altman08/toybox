/* lscpu.c - display CPU architecture information
 *
 * Copyright 2024 The Toybox Authors
 *
 * Sources:
 *   /proc/cpuinfo       - vendor, model, flags, topology
 *   /sys/devices/system/cpu/  - online/offline, cache, frequency
 *   /sys/devices/system/node/ - NUMA topology

USE_LSCPU(NEWTOY(lscpu, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config LSCPU
  bool "lscpu"
  default n
  help
    usage: lscpu

    Display CPU architecture information.
*/

#define FOR_lscpu
#include "toys.h"

// ---- ARM implementer/part lookup tables (from util-linux lscpu-arm.c) ------

struct id_part { int id; const char *name; };
struct hw_impl { int id; const struct id_part *parts; const char *name; };

static const struct id_part arm_part[] = {
  { 0x810, "ARM810" },       { 0x920, "ARM920" },
  { 0x922, "ARM922" },       { 0x926, "ARM926" },
  { 0x940, "ARM940" },       { 0x946, "ARM946" },
  { 0x966, "ARM966" },       { 0xa20, "ARM1020" },
  { 0xa22, "ARM1022" },      { 0xa26, "ARM1026" },
  { 0xb02, "ARM11-MPCore" }, { 0xb36, "ARM1136" },
  { 0xb56, "ARM1156" },      { 0xb76, "ARM1176" },
  { 0xc05, "Cortex-A5" },    { 0xc07, "Cortex-A7" },
  { 0xc08, "Cortex-A8" },    { 0xc09, "Cortex-A9" },
  { 0xc0d, "Cortex-A17" },   { 0xc0e, "Cortex-A17" },
  { 0xc0f, "Cortex-A15" },   { 0xc14, "Cortex-R4" },
  { 0xc15, "Cortex-R5" },    { 0xc17, "Cortex-R7" },
  { 0xc18, "Cortex-R8" },    { 0xc20, "Cortex-M0" },
  { 0xc21, "Cortex-M1" },    { 0xc23, "Cortex-M3" },
  { 0xc24, "Cortex-M4" },    { 0xc27, "Cortex-M7" },
  { 0xc60, "Cortex-M0+" },   { 0xd01, "Cortex-A32" },
  { 0xd02, "Cortex-A34" },   { 0xd03, "Cortex-A53" },
  { 0xd04, "Cortex-A35" },   { 0xd05, "Cortex-A55" },
  { 0xd06, "Cortex-A65" },   { 0xd07, "Cortex-A57" },
  { 0xd08, "Cortex-A72" },   { 0xd09, "Cortex-A73" },
  { 0xd0a, "Cortex-A75" },   { 0xd0b, "Cortex-A76" },
  { 0xd0c, "Neoverse-N1" },  { 0xd0d, "Cortex-A77" },
  { 0xd0e, "Cortex-A76AE" }, { 0xd13, "Cortex-R52" },
  { 0xd20, "Cortex-M23" },   { 0xd21, "Cortex-M33" },
  { 0xd40, "Neoverse-V1" },  { 0xd41, "Cortex-A78" },
  { 0xd42, "Cortex-A78AE" }, { 0xd44, "Cortex-X1" },
  { 0xd46, "Cortex-A510" },  { 0xd47, "Cortex-A710" },
  { 0xd48, "Cortex-X2" },    { 0xd49, "Neoverse-N2" },
  { 0xd4a, "Neoverse-E1" },  { 0xd4b, "Cortex-A78C" },
  { 0xd4c, "Cortex-X1C" },   { 0xd4d, "Cortex-A715" },
  { 0xd4e, "Cortex-X3" },    { 0xd4f, "Neoverse-V2" },
  { 0xd80, "Cortex-A520" },  { 0xd81, "Cortex-A720" },
  { 0xd82, "Cortex-X4" },    { 0xd84, "Neoverse-V3" },
  { 0xd85, "Cortex-X925" },  { 0xd87, "Cortex-A725" },
  { 0xd8e, "Neoverse-N3" },  { 0xd8f, "Cortex-A320" },
  { -1, NULL },
};
static const struct id_part brcm_part[] = {
  { 0x0f, "Brahma-B15" }, { 0x100, "Brahma-B53" }, { 0x516, "ThunderX2" },
  { -1, NULL },
};
static const struct id_part cavium_part[] = {
  { 0x0a0, "ThunderX" },    { 0x0a1, "ThunderX-88XX" },
  { 0x0a2, "ThunderX-81XX"}, { 0x0a3, "ThunderX-83XX" },
  { 0x0af, "ThunderX2-99xx"},{ 0x0b0, "OcteonTX2" },
  { 0x0b8, "ThunderX3-T110"},{ -1, NULL },
};
static const struct id_part dec_part[] = {
  { 0xa10, "SA110" }, { 0xa11, "SA1100" }, { -1, NULL },
};
static const struct id_part fujitsu_part[] = {
  { 0x001, "A64FX" }, { 0x003, "MONAKA" }, { -1, NULL },
};
static const struct id_part hisi_part[] = {
  { 0xd01, "TaiShan-v110" }, { 0xd02, "TaiShan-v120" },
  { 0xd40, "Cortex-A76" },   { 0xd41, "Cortex-A77" },
  { -1, NULL },
};
static const struct id_part nvidia_part[] = {
  { 0x000, "Denver" }, { 0x003, "Denver-2" }, { 0x004, "Carmel" },
  { -1, NULL },
};
static const struct id_part qcom_part[] = {
  { 0x001, "Oryon" },           { 0x00f, "Scorpion" },
  { 0x02d, "Scorpion" },        { 0x04d, "Krait" },
  { 0x06f, "Krait" },           { 0x201, "Kryo" },
  { 0x800, "Falkor-V1/Kryo" },  { 0x801, "Kryo-V2" },
  { 0x802, "Kryo-3XX-Gold" },   { 0x803, "Kryo-3XX-Silver" },
  { 0x804, "Kryo-4XX-Gold" },   { 0x805, "Kryo-4XX-Silver" },
  { 0xc00, "Falkor" },          { 0xc01, "Saphira" },
  { -1, NULL },
};
static const struct id_part samsung_part[] = {
  { 0x001, "exynos-m1" }, { 0x002, "exynos-m3" },
  { 0x003, "exynos-m4" }, { 0x004, "exynos-m5" },
  { -1, NULL },
};
static const struct id_part marvell_part[] = {
  { 0x131, "Feroceon-88FR131" }, { 0x581, "PJ4/PJ4b" }, { 0x584, "PJ4B-MP" },
  { -1, NULL },
};
static const struct id_part apple_part[] = {
  { 0x000, "Swift" },        { 0x001, "Cyclone" },
  { 0x002, "Typhoon" },      { 0x004, "Twister" },
  { 0x006, "Hurricane" },    { 0x008, "Monsoon" },
  { 0x009, "Mistral" },      { 0x00b, "Vortex" },
  { 0x00c, "Tempest" },      { 0x012, "Lightning" },
  { 0x013, "Thunder" },      { 0x020, "Icestorm-A14" },
  { 0x021, "Firestorm-A14" },{ 0x022, "Icestorm-M1" },
  { 0x023, "Firestorm-M1" }, { 0x030, "Blizzard-A15" },
  { 0x031, "Avalanche-A15" },{ 0x032, "Blizzard-M2" },
  { 0x033, "Avalanche-M2" }, { -1, NULL },
};
static const struct id_part ft_part[] = {
  { 0x303, "FTC310" }, { 0x660, "FTC660" }, { 0x662, "FTC662" },
  { 0x863, "FTC863" }, { -1, NULL },
};
static const struct id_part ms_part[] = {
  { 0xd49, "Azure-Cobalt-100" }, { -1, NULL },
};
static const struct id_part ampere_part[] = {
  { 0xac3, "Ampere-1" }, { 0xac4, "Ampere-1a" }, { -1, NULL },
};
static const struct id_part faraday_part[] = {
  { 0x526, "FA526" }, { 0x626, "FA626" }, { -1, NULL },
};
static const struct id_part intel_arm_part[] = {
  { 0x200, "i80200" },  { 0x411, "PXA27x" },
  { 0x682, "PXA32x" },  { 0x683, "PXA930/PXA935" },
  { 0xb11, "SA1110" },  { -1, NULL },
};
static const struct id_part unknown_part[] = { { -1, NULL } };

static const struct hw_impl hw_implementer[] = {
  { 0x41, arm_part,      "ARM" },
  { 0x42, brcm_part,     "Broadcom" },
  { 0x43, cavium_part,   "Cavium" },
  { 0x44, dec_part,      "DEC" },
  { 0x46, fujitsu_part,  "FUJITSU" },
  { 0x48, hisi_part,     "HiSilicon" },
  { 0x49, unknown_part,  "Infineon" },
  { 0x4d, unknown_part,  "Motorola/Freescale" },
  { 0x4e, nvidia_part,   "NVIDIA" },
  { 0x50, unknown_part,  "APM" },
  { 0x51, qcom_part,     "Qualcomm" },
  { 0x53, samsung_part,  "Samsung" },
  { 0x56, marvell_part,  "Marvell" },
  { 0x61, apple_part,    "Apple" },
  { 0x66, faraday_part,  "Faraday" },
  { 0x69, intel_arm_part,"Intel" },
  { 0x6d, ms_part,       "Microsoft" },
  { 0x70, ft_part,       "Phytium" },
  { 0xc0, ampere_part,   "Ampere" },
  { -1,   unknown_part,  NULL },
};

// Decode ARM implementer hex string (e.g. "0x41") -> vendor name + part name
// Returns 1 if an ARM implementer was recognised
static int arm_decode(const char *impl_str, const char *part_str,
                      char *vendor_out, int vendor_len,
                      char *model_out,  int model_len)
{
  int impl_id, part_id, j;
  char *end;

  if (!impl_str || strncmp(impl_str, "0x", 2)) return 0;
  impl_id = (int)strtol(impl_str, &end, 0);
  if (end == impl_str) return 0;

  for (j = 0; hw_implementer[j].id != -1; j++) {
    if (hw_implementer[j].id == impl_id) {
      const struct id_part *parts = hw_implementer[j].parts;

      snprintf(vendor_out, vendor_len, "%s", hw_implementer[j].name);

      // decode part -> model name
      if (part_str && !strncmp(part_str, "0x", 2)) {
        part_id = (int)strtol(part_str, &end, 0);
        if (end != part_str) {
          int k;
          for (k = 0; parts[k].id != -1; k++) {
            if (parts[k].id == part_id) {
              snprintf(model_out, model_len, "%s", parts[k].name);
              break;
            }
          }
        }
      }
      return 1;
    }
  }
  return 0;
}

// ---- sysfs helpers ----------------------------------------------------------

// Read a sysfs file, trim trailing whitespace, return 1 on success
static int sysfs_read(char *buf, int buflen, char *fmt, ...)
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

// Read a numeric value from sysfs, return -1 on failure
static long long sysfs_num(char *fmt, ...)
{
  char path[256], buf[64];
  va_list va;

  va_start(va, fmt);
  vsnprintf(path, sizeof(path), fmt, va);
  va_end(va);

  if (!readfile(path, buf, sizeof(buf) - 1)) return -1;
  return atoll(buf);
}

// Count entries in /sys/devices/system/X/ matching a prefix
static int sysfs_count(char *dir, char *prefix)
{
  DIR *dp = opendir(dir);
  struct dirent *de;
  int count = 0, plen = strlen(prefix);

  if (!dp) return 0;
  while ((de = readdir(dp)))
    if (!strncmp(de->d_name, prefix, plen) && isdigit(de->d_name[plen]))
      count++;
  closedir(dp);
  return count;
}

// Parse a cpulist range string like "0-15,32-47" and count entries
static int cpulist_count(char *list)
{
  int count = 0;
  char *p = list;

  while (*p) {
    long start = strtol(p, &p, 10);
    long end = start;

    if (*p == '-') { p++; end = strtol(p, &p, 10); }
    count += end - start + 1;
    if (*p == ',') p++;
  }
  return count;
}

// ---- /proc/cpuinfo parser ---------------------------------------------------

// Read the first field from /proc/cpuinfo whose key matches exactly (case
// sensitive).  The key in /proc/cpuinfo is separated from its value by
// optional whitespace then ':'.  We require that after the key prefix the
// next non-space character is ':', so "model" does NOT match "model name".
// Returns 1 on success, 0 if not found.
static int cpuinfo_field(char *out, int outlen, const char *key)
{
  FILE *fp = xfopen("/proc/cpuinfo", "r");
  char *line;
  int found = 0, klen = strlen(key);

  while ((line = xgetline(fp))) {
    if (!strncmp(line, key, klen)) {
      // after key prefix, skip whitespace then expect ':'
      char *p = line + klen;
      while (*p == ' ' || *p == '\t') p++;
      if (*p == ':') {
        p++;
        while (*p == ' ' || *p == '\t') p++;
        snprintf(out, outlen, "%s", p);
        chomp(out);
        found = 1;
        free(line);
        break;
      }
    }
    free(line);
  }
  fclose(fp);
  return found;
}

// Offline CPU list: possible & ~online, expressed as a cpulist string
static void compute_offline(char *out, int outlen, char *possible, char *online)
{
  char pmap[128], omap[128];
  int pmax = 0, i, started = -1;
  char *p;

  memset(pmap, 0, sizeof(pmap));
  memset(omap, 0, sizeof(omap));

  p = possible;
  while (*p) {
    long s = strtol(p, &p, 10), e = s;
    if (*p == '-') { p++; e = strtol(p, &p, 10); }
    for (i = s; i <= e && i < (int)sizeof(pmap)*8; i++) pmap[i] = 1;
    if (e+1 > pmax) pmax = e+1;
    if (*p == ',') p++;
  }

  p = online;
  while (*p) {
    long s = strtol(p, &p, 10), e = s;
    if (*p == '-') { p++; e = strtol(p, &p, 10); }
    for (i = s; i <= e && i < (int)sizeof(omap)*8; i++) omap[i] = 1;
    if (*p == ',') p++;
  }

  out[0] = 0;
  for (i = 0; i < pmax; i++) {
    if (pmap[i] && !omap[i]) {
      if (started < 0) started = i;
    } else if (started >= 0) {
      int len = strlen(out);
      if (started == i-1)
        snprintf(out+len, outlen-len, "%s%d", len ? "," : "", started);
      else
        snprintf(out+len, outlen-len, "%s%d-%d", len ? "," : "", started, i-1);
      started = -1;
    }
  }
  if (started >= 0) {
    int len = strlen(out);
    if (started == pmax-1)
      snprintf(out+len, outlen-len, "%s%d", len ? "," : "", started);
    else
      snprintf(out+len, outlen-len, "%s%d-%d", len ? "," : "", started, pmax-1);
  }
}

void lscpu_main(void)
{
  char *buf = toybuf;
  char arch[65];
  char vendor[64]     = "";
  char model_name[128]= "";
  char cpu_possible[64], cpu_online[64], cpu_offline[64];
  int total_cpus, online_cpus, sockets, cores_per_socket;
  int threads_per_core, numa_nodes;
  long long max_mhz, min_mhz, cur_mhz;
  char l1d[16], l1i[16], l2[16], l3[16];

  // Architecture from uname
  struct utsname uts;
  uname(&uts);
  snprintf(arch, sizeof(arch), "%s", uts.machine);

  // CPU counts from sysfs
  if (!sysfs_read(cpu_possible, sizeof(cpu_possible),
                  "/sys/devices/system/cpu/possible"))
    snprintf(cpu_possible, sizeof(cpu_possible), "?");
  if (!sysfs_read(cpu_online, sizeof(cpu_online),
                  "/sys/devices/system/cpu/online"))
    snprintf(cpu_online, sizeof(cpu_online), "?");

  compute_offline(cpu_offline, sizeof(cpu_offline), cpu_possible, cpu_online);

  total_cpus  = sysfs_count("/sys/devices/system/cpu", "cpu");
  online_cpus = cpulist_count(cpu_online);

  // Vendor and model name from /proc/cpuinfo
  // x86: "vendor_id" / "model name"
  // ARM: "CPU implementer" (hex) + "CPU part" (hex) -> decode via lookup table
  {
    char impl[32] = "", part[32] = "";

    // Try x86-style fields first
    cpuinfo_field(vendor,     sizeof(vendor),     "vendor_id");
    cpuinfo_field(model_name, sizeof(model_name), "model name");

    // ARM: "CPU implementer" present means we need ID-based decoding
    if (cpuinfo_field(impl, sizeof(impl), "CPU implementer")) {
      cpuinfo_field(part, sizeof(part), "CPU part");
      // arm_decode fills vendor and model_name (only overwrites if found)
      arm_decode(impl, part, vendor, sizeof(vendor),
                 model_name, sizeof(model_name));
    }
  }

  // Socket count from sysfs topology
  {
    int i, max_pkg = -1;

    for (i = 0; i < total_cpus; i++) {
      long long pkg = sysfs_num(
        "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", i);
      if (pkg > max_pkg) max_pkg = pkg;
    }
    sockets = max_pkg + 1;
    if (sockets < 1) sockets = 1;
  }

  // Core/thread topology
  {
    int i;
    char tslist[64], cslist[128];

    if (sysfs_read(tslist, sizeof(tslist),
                   "/sys/devices/system/cpu/cpu0/topology/thread_siblings_list")) {
      threads_per_core = 0;
      char *p = tslist;
      while (*p) {
        long s = strtol(p, &p, 10), e = s;
        if (*p == '-') { p++; e = strtol(p, &p, 10); }
        for (i = s; i <= e; i++) {
          char online_str[8];
          if (sysfs_read(online_str, sizeof(online_str),
                         "/sys/devices/system/cpu/cpu%d/online", i))
            threads_per_core += (online_str[0] == '1' ||
                                 (!i && online_str[0] == '\0'));
          else if (i == 0) threads_per_core++;
        }
        if (*p == ',') p++;
      }
    } else threads_per_core = 1;

    if (sysfs_read(cslist, sizeof(cslist),
                   "/sys/devices/system/cpu/cpu0/topology/core_siblings_list"))
      cores_per_socket = cpulist_count(cslist) / cpulist_count(tslist);
    else cores_per_socket = online_cpus / sockets / threads_per_core;

    if (threads_per_core < 1) threads_per_core = 1;
    if (cores_per_socket < 1) cores_per_socket = 1;
  }

  // NUMA nodes
  numa_nodes = sysfs_count("/sys/devices/system/node", "node");
  if (!numa_nodes) numa_nodes = 1;

  // CPU frequency (kHz -> MHz)
  max_mhz = sysfs_num("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
  min_mhz = sysfs_num("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_min_freq");
  cur_mhz = sysfs_num("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
  if (max_mhz < 0) {
    if (cpuinfo_field(buf, 64, "cpu MHz")) cur_mhz = atof(buf) * 1000;
  }

  // Cache from sysfs (cpu0)
  *l1d = *l1i = *l2 = *l3 = 0;
  {
    int idx;

    for (idx = 0; idx < 8; idx++) {
      char level_str[8], type_str[32], size_str[16];

      if (!sysfs_read(level_str, sizeof(level_str),
                      "/sys/devices/system/cpu/cpu0/cache/index%d/level", idx))
        break;
      sysfs_read(type_str, sizeof(type_str),
                 "/sys/devices/system/cpu/cpu0/cache/index%d/type", idx);
      sysfs_read(size_str, sizeof(size_str),
                 "/sys/devices/system/cpu/cpu0/cache/index%d/size", idx);

      int level = atoi(level_str);

      if (level == 1) {
        if (!strcmp(type_str, "Data"))
          snprintf(l1d, sizeof(l1d), "%s", size_str);
        else if (!strcmp(type_str, "Instruction"))
          snprintf(l1i, sizeof(l1i), "%s", size_str);
      } else if (level == 2) snprintf(l2, sizeof(l2), "%s", size_str);
      else if (level == 3) snprintf(l3, sizeof(l3), "%s", size_str);
    }
  }

  // BogoMIPS: x86 uses "bogomips", ARM uses "BogoMIPS"
  char bogomips[32] = "";
  if (!cpuinfo_field(bogomips, sizeof(bogomips), "bogomips"))
    cpuinfo_field(bogomips, sizeof(bogomips), "BogoMIPS");

  // CPU flags/features: x86 "flags", ARM "Features"
  char cpu_flags[4096] = "";
  if (!cpuinfo_field(cpu_flags, sizeof(cpu_flags), "flags"))
    cpuinfo_field(cpu_flags, sizeof(cpu_flags), "Features");

  // Virtualization: check flags for vmx (Intel VT-x) or svm (AMD-V)
  char virt[16] = "";
  if (*cpu_flags) {
    if (strstr(cpu_flags, "vmx"))      snprintf(virt, sizeof(virt), "VT-x");
    else if (strstr(cpu_flags, "svm")) snprintf(virt, sizeof(virt), "AMD-V");
  }

  // --- Output ---
  // Indentation levels matching util-linux lscpu:
  //   L0 (no indent):  Architecture, CPU(s), Vendor ID, NUMA node(s), caches
  //   L1 (2 spaces):   CPU op-mode(s), Byte Order, On/Off-line list, Model name
  //   L2 (4 spaces):   Model, Thread/Core/Socket, freq, BogoMIPS, Flags, Stepping
#define P0(key, fmt, ...) printf("%-22s " fmt "\n", key, ##__VA_ARGS__)
#define P1(key, fmt, ...) printf("  %-20s " fmt "\n", key, ##__VA_ARGS__)
#define P2(key, fmt, ...) printf("    %-18s " fmt "\n", key, ##__VA_ARGS__)

  P0("Architecture:", "%s", arch);

  // op-mode (L1: sub-field of Architecture)
  if (!strcmp(arch, "x86_64"))
    P1("CPU op-mode(s):", "%s", "32-bit, 64-bit");
  else if (strstr(arch, "64"))
    P1("CPU op-mode(s):", "%s", "32-bit, 64-bit");
  else
    P1("CPU op-mode(s):", "%s", "32-bit");

  P1("Byte Order:", "%s",
    (*(char *)&(int){1}) ? "Little Endian" : "Big Endian");

  P0("CPU(s):", "%d", total_cpus);
  if (*cpu_online)
    P1("On-line CPU(s) list:", "%s", cpu_online);
  if (*cpu_offline)
    P1("Off-line CPU(s) list:", "%s", cpu_offline);

  if (*vendor)     P0("Vendor ID:", "%s", vendor);

  if (*model_name) P1("Model name:", "%s", model_name);

  // x86 specific fields (L2: sub-fields of Model name)
  if (cpuinfo_field(buf, 256, "cpu family"))
    P2("CPU family:", "%s", buf);
  if (cpuinfo_field(buf, 256, "model"))
    P2("Model:", "%s", buf);
  if (cpuinfo_field(buf, 256, "stepping"))
    P2("Stepping:", "%s", buf);

  P2("Thread(s) per core:", "%d", threads_per_core);
  P2("Core(s) per socket:", "%d", cores_per_socket);
  P2("Socket(s):", "%d", sockets);
  P2("NUMA node(s):", "%d", numa_nodes);

  if (cur_mhz > 0)
    P2("CPU MHz:", "%.3f", cur_mhz / 1000.0);
  if (max_mhz > 0)
    P2("CPU max MHz:", "%.4f", max_mhz / 1000.0);
  if (min_mhz > 0)
    P2("CPU min MHz:", "%.4f", min_mhz / 1000.0);

  if (*bogomips)   P2("BogoMIPS:", "%s", bogomips);
  if (*virt)       P2("Virtualization:", "%s", virt);

  if (*l1d) P0("L1d cache:", "%s", l1d);
  if (*l1i) P0("L1i cache:", "%s", l1i);
  if (*l2)  P0("L2 cache:", "%s", l2);
  if (*l3)  P0("L3 cache:", "%s", l3);

  // NUMA node CPU lists
  {
    int node;

    for (node = 0; node < numa_nodes; node++) {
      char cpulist[128];
      char label[32];

      snprintf(label, sizeof(label), "NUMA node%d CPU(s):", node);
      if (sysfs_read(cpulist, sizeof(cpulist),
                     "/sys/devices/system/node/node%d/cpulist", node))
        P1(label, "%s", cpulist);
    }
  }

  // Flags (L2: sub-field of Model name)
  if (*cpu_flags)
    P2("Flags:", "%s", cpu_flags);

#undef P0
#undef P1
#undef P2
}
