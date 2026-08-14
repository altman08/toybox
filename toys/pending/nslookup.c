/* nslookup.c - query DNS name servers
 *
 * Copyright 2024 Rob Landley <rob@landley.net>
 *
 * No standard, see https://www.ietf.org/rfc/rfc1035.txt
 * Behavior loosely follows bind-utils nslookup / busybox nslookup.

USE_NSLOOKUP(NEWTOY(nslookup, "<1>2t:", TOYFLAG_USR|TOYFLAG_BIN))

config NSLOOKUP
  bool "nslookup"
  default y
  help
    usage: nslookup [-t TYPE] HOST [SERVER]

    Query DNS for information about HOST, using SERVER or /etc/resolv.conf.

    -t TYPE	Query record type: A AAAA CNAME MX NS PTR SOA SRV TXT ANY
*/

#define FOR_nslookup
#include "toys.h"
#include <resolv.h>
#include <arpa/inet.h>

GLOBALS(
  char *t;

  char **nsname;
  unsigned nslen;
)

// Supported record types table
static const struct {
  char name[8];
  int  type;
} qtypes[] = {
  { "A",     1   },
  { "NS",    2   },
  { "CNAME", 5   },
  { "SOA",   6   },
  { "PTR",   12  },
  { "MX",    15  },
  { "TXT",   16  },
  { "AAAA",  28  },
  { "SRV",   33  },
  { "ANY",   255 },
};

static const char *rcodes[] = {
  "NOERROR", "FORMERR", "SERVFAIL", "NXDOMAIN",
  "NOTIMP",  "REFUSED", "YXDOMAIN", "YXRRSET",
  "NXRRSET", "NOTAUTH", "NOTZONE",
};

// Parse "nameserver" lines from /etc/resolv.conf
static void get_nsname(char **pline, long len)
{
  char *line, *p;

  if (!len) return;
  line = *pline;
  if (strstart(&line, "nameserver") && isspace(*line)) {
    while (isspace(*line)) line++;
    for (p = line; *p && !isspace(*p) && *p != '#'; p++);
    if (p == line) return;
    *p = 0;
    if (!(TT.nslen & 7))
      TT.nsname = xrealloc(TT.nsname, (TT.nslen + 8) * sizeof(char *));
    TT.nsname[TT.nslen++] = xstrdup(line);
  }
}

// Resolve type name string to numeric type; returns -1 on unknown
static int parse_type(const char *s)
{
  int i;

  if (*s >= '0' && *s <= '9') return atoi(s);
  for (i = 0; i < ARRAY_LEN(qtypes); i++)
    if (!strcasecmp(s, qtypes[i].name)) return qtypes[i].type;
  return -1;
}

// Return type name string for numeric type
static const char *type_name(int type)
{
  int i;

  for (i = 0; i < ARRAY_LEN(qtypes); i++)
    if (qtypes[i].type == type) return qtypes[i].name;
  return "UNKNOWN";
}

// Parse and print answer section RRs; return number of records printed
static int parse_reply(char *abuf, int alen, const char *qname)
{
  char expand[1025], tmp[INET6_ADDRSTRLEN];
  unsigned char *msg = (unsigned char *)abuf;
  unsigned char *end = msg + alen;
  unsigned char *p;
  int ancount, i, rdlen, rrtype, n, printed = 0;

  // DNS header: flags byte 2 bit 2 = AA
  if (!(abuf[2] & 4)) printf("Non-authoritative answer:\n");

  ancount = peek_be(abuf + 6, 2);

  // Skip question section: 12-byte header + encoded qname + 4 bytes type/class
  p = msg + 12;
  // skip qname (sequence of length-prefixed labels ending with 0)
  while (p < end && *p) {
    if ((*p & 0xc0) == 0xc0) { p += 2; break; } // compression pointer
    p += *p + 1;
  }
  if (p < end && !(*p & 0xc0)) p++;  // terminating zero
  p += 4;  // skip QTYPE + QCLASS

  for (i = 0; i < ancount && p < end; i++) {
    // Expand owner name
    n = dn_expand(msg, end, p, expand, sizeof(expand));
    if (n < 0) break;
    p += n;
    if (p + 10 > end) break;

    rrtype = peek_be(p, 2);
    // skip type(2) + class(2)
    p += 4;
    p += 4; // skip TTL
    rdlen = peek_be(p, 2);
    p += 2;
    if (p + rdlen > end) break;

    switch (rrtype) {
    case 1:  // A
      if (rdlen != 4) break;
      inet_ntop(AF_INET, p, tmp, sizeof(tmp));
      printf("Name:\t%s\nAddress: %s\n", expand, tmp);
      printed++;
      break;

    case 28: // AAAA
      if (rdlen != 16) break;
      inet_ntop(AF_INET6, p, tmp, sizeof(tmp));
      printf("Name:\t%s\nAddress: %s\n", expand, tmp);
      printed++;
      break;

    case 5:  // CNAME
      n = dn_expand(msg, end, p, tmp, sizeof(tmp));
      if (n < 0) break;
      printf("%s\tcanonical name = %s\n", expand, tmp);
      printed++;
      break;

    case 2:  // NS
      n = dn_expand(msg, end, p, tmp, sizeof(tmp));
      if (n < 0) break;
      printf("%s\tnameserver = %s\n", expand, tmp);
      printed++;
      break;

    case 12: // PTR
      n = dn_expand(msg, end, p, tmp, sizeof(tmp));
      if (n < 0) break;
      printf("%s\tname = %s\n", expand, tmp);
      printed++;
      break;

    case 15: { // MX
      int pref;
      if (rdlen < 3) break;
      pref = peek_be(p, 2);
      n = dn_expand(msg, end, p + 2, tmp, sizeof(tmp));
      if (n < 0) break;
      printf("%s\tmail exchanger = %d %s\n", expand, pref, tmp);
      printed++;
      break;
    }

    case 16: { // TXT
      int tlen;
      if (rdlen < 1) break;
      tlen = p[0];
      if (tlen > rdlen - 1) tlen = rdlen - 1;
      printf("%s\ttext = \"%.*s\"\n", expand, tlen, (char *)p + 1);
      printed++;
      break;
    }

    case 33: { // SRV
      unsigned prio, weight, port;
      if (rdlen < 7) break;
      prio   = peek_be(p,     2);
      weight = peek_be(p + 2, 2);
      port   = peek_be(p + 4, 2);
      n = dn_expand(msg, end, p + 6, tmp, sizeof(tmp));
      if (n < 0) break;
      printf("%s\tservice = %u %u %u %s\n", expand, prio, weight, port, tmp);
      printed++;
      break;
    }

    case 6: { // SOA
      char mname[1025], rname[1025];
      unsigned char *cp = p;
      n = dn_expand(msg, end, cp, mname, sizeof(mname));
      if (n < 0) break;
      cp += n;
      n = dn_expand(msg, end, cp, rname, sizeof(rname));
      if (n < 0) break;
      cp += n;
      if (cp + 20 > end) break;
      printf("%s\n\torigin = %s\n\tmail addr = %s\n"
             "\tserial = %lu\n\trefresh = %lu\n\tretry = %lu\n"
             "\texpire = %lu\n\tminimum = %lu\n",
             expand, mname, rname,
             (unsigned long)peek_be(cp,    4),
             (unsigned long)peek_be(cp+4,  4),
             (unsigned long)peek_be(cp+8,  4),
             (unsigned long)peek_be(cp+12, 4),
             (unsigned long)peek_be(cp+16, 4));
      printed++;
      break;
    }

    default:
      printf("%s\t%s record\n", expand, type_name(rrtype));
      printed++;
      break;
    }

    p += rdlen;
  }

  return printed;
}

// Build PTR name for reverse lookup (e.g. "1.2.3.4" -> "4.3.2.1.in-addr.arpa")
static char *make_ptr(const char *addr)
{
  unsigned char buf[16];

  if (inet_pton(AF_INET, addr, buf))
    return xmprintf("%u.%u.%u.%u.in-addr.arpa",
                    buf[3], buf[2], buf[1], buf[0]);
  if (inet_pton(AF_INET6, addr, buf)) {
    char rev[80];
    int i, j;
    for (j = 0, i = 15; i >= 0; i--) {
      j += sprintf(rev + j, "%x.%x.", buf[i] & 0xf, buf[i] >> 4);
    }
    strcpy(rev + j, "ip6.arpa");
    return xstrdup(rev);
  }
  return NULL;
}

void nslookup_main(void)
{
  char *qbuf = toybuf + 2048;    // query packet (fits in toybuf upper half)
  int qbuflen = 2048;
  char *abuf;
  int alen, qlen, type, i, rcode;
  char *name = toys.optargs[0];
  char *ptr;
  struct addrinfo *ai;

  // Determine query type
  if (TT.t) {
    type = parse_type(TT.t);
    if (type < 0) error_exit("unknown query type: %s", TT.t);
  } else {
    // If name looks like an IP address, do PTR lookup; else A (+ AAAA)
    ptr = make_ptr(name);
    if (ptr) { name = ptr; type = 12; }  // PTR
    else type = 1;                        // A
  }

  // Build DNS query packet
  qlen = res_mkquery(0, name, 1, type, 0, 0, 0, (unsigned char *)qbuf, qbuflen);
  if (qlen < 0) error_exit("bad NAME: %s", name);

  // Collect name servers
  if (toys.optargs[1]) {
    // Single server given on command line
    TT.nsname = toys.optargs + 1;
    TT.nslen  = 1;
  } else {
    do_lines(xopen("/etc/resolv.conf", O_RDONLY), '\n', get_nsname);
    if (!TT.nslen) {
      // Fall back to localhost
      TT.nsname = xmalloc(2 * sizeof(char *));
      TT.nsname[0] = "127.0.0.1";
      TT.nsname[1] = NULL;
      TT.nslen = 1;
    }
  }

  // Try each name server in turn
  abuf = NULL;
  for (i = 0; i < (int)TT.nslen; i++) {
    ai = xgetaddrinfo(TT.nsname[i], "53", 0, SOCK_DGRAM, 0, 0);
    int fd = xsocket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    xconnect(fd, ai->ai_addr, ai->ai_addrlen);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
               &(struct timeval){.tv_sec = 5}, sizeof(struct timeval));
    send(fd, qbuf, qlen, 0);
    abuf = xmalloc(65536);
    alen = recv(fd, abuf, 65536, 0);
    close(fd);
    if (alen > 12) break;
    free(abuf);
    abuf = NULL;
  }

  if (!abuf) error_exit(";; connection timed out; no servers could be reached");

  // Print server info
  printf("Server:\t\t%s\nAddress:\t%s#53\n\n",
         TT.nsname[i < (int)TT.nslen ? i : (int)TT.nslen - 1],
         TT.nsname[i < (int)TT.nslen ? i : (int)TT.nslen - 1]);

  // Check rcode
  rcode = abuf[3] & 0x0f;
  if (rcode != 0) {
    const char *rs = rcode < (int)ARRAY_LEN(rcodes) ? rcodes[rcode] : "UNKNOWN";
    printf("** server can't find %s: %s\n", toys.optargs[0], rs);
    toys.exitval = 1;
  } else if (parse_reply(abuf, alen, name) == 0) {
    printf("*** Can't find %s: No answer\n", toys.optargs[0]);
    toys.exitval = 1;
  }

  if (CFG_TOYBOX_FREE) {
    free(abuf);
    if (toys.optargs[1] && TT.nsname != toys.optargs + 1) free(TT.nsname);
  }
}
