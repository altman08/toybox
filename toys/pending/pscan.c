/* pscan.c - simple network port scanner
 *
 * Copyright 2007 Tito Ragusa <farmatito@tiscali.it>
 *
 * No standard.

USE_PSCAN(NEWTOY(pscan, "<1>1cbp#<1>65535=1P#<1>65535=1024t#<1=5000T#<1=5", TOYFLAG_USR|TOYFLAG_BIN))

config PSCAN
  bool "pscan"
  default n
  help
    usage: pscan [-cb] [-p MIN_PORT] [-P MAX_PORT] [-t TIMEOUT] [-T MIN_RTT] HOST

    Scan HOST and print open ports.

    -c  Show closed ports too
    -b  Show blocked (timed out) ports too
    -p  First port to scan (default 1)
    -P  Last port to scan (default 1024)
    -t  Timeout in milliseconds (default 5000)
    -T  Minimum RTT in milliseconds (default 5)
*/

#define FOR_pscan
#include "toys.h"

GLOBALS(
  long T;
  long t;
  long P;
  long p;
)

// Look up the service name for a port, return "unknown" if not found.
static const char *pscan_service(unsigned port)
{
  struct servent *se = getservbyport(htons(port), "tcp");
  return se ? se->s_name : "unknown";
}

void pscan_main(void)
{
  char *host = toys.optargs[0];
  char portstr[8];
  struct addrinfo *ai, *aip;
  unsigned port, max_port = (unsigned)TT.P;
  unsigned open_ports = 0, closed_ports = 0, nports;
  // All times in microseconds.
  long long timeout  = TT.t * 1000LL;
  long long min_rtt  = TT.T * 1000LL;
  long long rtt_4    = timeout;   // initial rtt estimate: full timeout

  if (TT.p > TT.P) error_exit("min port %ld > max port %ld", TT.p, TT.P);
  port = (unsigned)TT.p;
  nports = max_port - port + 1;

  // Resolve host once; we'll swap the port in the sockaddr for each probe.
  snprintf(portstr, sizeof(portstr), "%u", port);
  aip = xgetaddrinfo(host, portstr, AF_UNSPEC, SOCK_STREAM, 0, 0);
  // Use only the first address.
  ai = aip;

  xprintf("Scanning %s ports %u to %u\n Port\tProto\tState\tService\n",
          host, port, max_port);

  for (; port <= max_port; port++) {
    int s, r;
    long long start, diff;
    const char *result_str = NULL;

    // Patch port number into the resolved sockaddr.
    if (ai->ai_family == AF_INET)
      ((struct sockaddr_in *)ai->ai_addr)->sin_port = htons(port);
    else
      ((struct sockaddr_in6 *)ai->ai_addr)->sin6_port = htons(port);

    s = socket(ai->ai_family, SOCK_STREAM, 0);
    if (s < 0) perror_exit("socket");
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);

    start = millitime() * 1000LL;
    r = connect(s, ai->ai_addr, ai->ai_addrlen);
    if (r == 0) goto open;
    if (errno != EAGAIN && errno != EINPROGRESS && errno != ECONNREFUSED)
      perror_exit("connect");

    diff = 0;
    while (1) {
      if (errno == ECONNREFUSED) {
        if (FLAG(c)) result_str = "closed";
        closed_ports++;
        break;
      }
      if (diff > rtt_4) {
        if (FLAG(b)) result_str = "blocked";
        break;
      }
      usleep(rtt_4 / 8);
open:
      diff = millitime() * 1000LL - start;
      if (write(s, " ", 1) >= 0) {
        open_ports++;
        result_str = "open";
        break;
      }
    }
    close(s);

    if (result_str)
      xprintf("%5u\ttcp\t%s\t%s\n", port, result_str, pscan_service(port));

    // Update RTT estimate: increase fast, decrease slow.
    rtt_4 = diff * 4;
    if (rtt_4 < min_rtt) rtt_4 = min_rtt;
    if (rtt_4 > timeout)  rtt_4 = timeout;
  }
  freeaddrinfo(aip);

  xprintf("%u closed, %u open, %u timed out (or blocked) ports\n",
          closed_ports, open_ports, nports - (closed_ports + open_ports));
}
