/* whois.c - tiny client for the whois directory service
 *
 * Copyright 2011 Pere Orga <gotrunks@gmail.com>
 *
 * No standard.
 * See https://www.rfc-editor.org/rfc/rfc3912

USE_WHOIS(NEWTOY(whois, "<1h:p#<1>65535=43i", TOYFLAG_USR|TOYFLAG_BIN))

config WHOIS
  bool "whois"
  default n
  help
    usage: whois [-i] [-h SERVER] [-p PORT] NAME...

    Query WHOIS info about NAME.

    -h  Server to query (default whois.iana.org)
    -i  Show redirect results too
    -p  Port to use (default 43)
*/

#define FOR_whois
#include "toys.h"

GLOBALS(
  long p;
  char *h;
)

// Perform one TCP query to <host>:<port>, send "<pfx><domain>\r\n",
// read and accumulate the response (up to 32 KB).
// Returns allocated response buffer (caller frees), sets *redir_out if
// a "whois server:" redirect was found and *success_out if the reply
// contained a "domain:" line.
static char *whois_do_request(const char *host, int port, const char *pfx,
                              const char *domain, int *success_out,
                              char **redir_out)
{
  char portstr[8], *buf = NULL, linebuf[2048];
  unsigned bufpos = 0;
  FILE *fp;
  int fd, i;

  snprintf(portstr, sizeof(portstr), "%d", port);
  xprintf("[Querying %s:%d '%s%s']\n", host, port, pfx, domain);
  fd = xconnectany(xgetaddrinfo((char *)host, portstr,
                                AF_UNSPEC, SOCK_STREAM, 0, 0));
  dprintf(fd, "%s%s\r\n", pfx, domain);
  fp = fdopen(fd, "r");
  if (!fp) perror_exit("fdopen");

  *success_out = 0;
  *redir_out = NULL;

  while (bufpos < 32*1024 && fgets(linebuf, sizeof(linebuf)-1, fp)) {
    unsigned len = strcspn(linebuf, "\r\n");
    char tmp[2048];

    linebuf[len] = '\n';
    linebuf[len+1] = '\0';
    len++;

    buf = xrealloc(buf, bufpos + len + 1);
    memcpy(buf + bufpos, linebuf, len);
    bufpos += len;
    buf[bufpos] = '\0';

    // Work on a lowercase copy for keyword matching.
    memcpy(tmp, linebuf, len);
    tmp[len] = '\0';
    // Strip trailing whitespace.
    while (len > 0 && (tmp[len-1]=='\n' || tmp[len-1]==' ' || tmp[len-1]=='\r'))
      tmp[--len] = '\0';
    for (i = 0; tmp[i]; i++) tmp[i] = tolower((unsigned char)tmp[i]);

    if (!*success_out)
      *success_out = !strncmp(tmp, "domain:", 7) ||
                     !strncmp(tmp, "domain name:", 12);

    if (*success_out && !*redir_out) {
      char *p = NULL;

      if (!strncmp(tmp, "whois server:", 13))      p = tmp + 13;
      else if (!strncmp(tmp, "whois:", 6))          p = tmp + 6;
      if (p) {
        while (*p == ' ') p++;
        *redir_out = xstrdup(p);
      }
    }
  }
  fclose(fp);
  return buf;
}

// Query <host>:<port> for <domain>. Print result, return redirect host
// (or NULL). Caller must free returned string.
static char *whois_query(const char *host, int port, const char *domain)
{
  char *buf, *redir;
  int success;

  buf = whois_do_request(host, port, "", domain, &success, &redir);

  // Some servers need "domain DOMAIN" format; retry once.
  if (!success && !redir) {
    free(buf);
    buf = whois_do_request(host, port, "domain ", domain, &success, &redir);
  }

  // Redirect to self doesn't count.
  if (redir && !strcmp(redir, host)) { free(redir); redir = NULL; }

  // Print output if no redirect or -i is set.
  if (!redir || FLAG(i)) xprintf("[%s]\n%s", host, buf ? buf : "");

  free(buf);
  return redir;
}

void whois_main(void)
{
  const char *host = TT.h ? TT.h : "whois.iana.org";
  char **argv = toys.optargs;

  do {
    char *free_me = NULL, *redir;
    const char *cur = host;
    int port = (int)TT.p;

    for (;;) {
      redir = whois_query(cur, port, *argv);
      free(free_me);
      if (!redir) break;
      xprintf("[Redirected to %s]\n", redir);
      cur = free_me = redir;
      port = 43;
    }
  } while (*++argv);
}
