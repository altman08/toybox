/* inetd.c - Internet super-server daemon
 *
 * Copyright 2024 The Toybox Authors
 *
 * Derived from busybox inetd (Vladimir Oleynik, Denys Vlasenko et al.)
 * Original copyright (c) 1983,1991 The Regents of the University of California.
 *
 * inetd.conf format (each non-comment line):
 *   [host:]service  stream|dgram  tcp|udp[6]  wait|nowait[.max]
 *                   user[.group]  /path/prog  [args...]
 *
 * A line of just "host:" changes the default bind address for following lines.
 * '*' means INADDR_ANY.

USE_INETD(NEWTOY(inetd, "feq#<0=128R#<0=0", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_STAYROOT))

config INETD
  bool "inetd"
  default n
  depends on TOYBOX_FORK
  help
    usage: inetd [-fe] [-q N] [-R N] [CONFFILE]

    Internet super-server daemon. Listen on configured ports and launch
    programs to handle incoming connections.

    -f      Run in foreground (don't daemonize)
    -e      Log to stderr instead of syslog
    -q N    Socket listen queue length (default 128)
    -R N    Max connections per minute before pausing service (default 0=off)
    CONFFILE  Config file (default /etc/inetd.conf)

    Config file format (one service per line):
      [host:]port stream|dgram tcp|udp[6] wait|nowait[.max] user[.group]
                  /path/prog [args...]

    Lines starting with '#' are comments. A line of just "host:" sets the
    default bind address for subsequent entries ('*' means all interfaces).
    'wait' runs one child at a time; 'nowait' forks per connection.
    'internal' as program runs a built-in service (echo, discard, time,
    daytime, chargen).
*/

#define FOR_inetd
#include "toys.h"

GLOBALS(
  long q;
  long R;

  struct servtab *svcs;        /* linked list of services */
  fd_set rfds;                 /* master read-fd set */
  int maxfd;                   /* highest fd in rfds */
  char *defhost;               /* default bind hostname ('*' = any) */
  int alarm_armed;             /* SIGALRM scheduled? */
)

#define CNT_INTERVAL  60       /* rate-limit window in seconds */
#define RETRYTIME     60       /* seconds between bind retries */
#define MAXARGS       20       /* max argv entries per service */

struct servtab {
  struct servtab *next;
  char   *host;                /* bind address (NULL = use defhost) */
  char   *service;             /* port name/number */
  char   *proto;               /* "tcp", "udp", "tcp6", "udp6" */
  int     socktype;            /* SOCK_STREAM or SOCK_DGRAM */
  int     family;              /* AF_INET or AF_INET6 */
  int     wait;                /* 0=nowait, 1=wait, >1=pid of waiting child */
  unsigned max;                /* max conns/minute (0=unlimited) */
  unsigned count;              /* connections since se_time */
  unsigned setime;             /* time of first connection in window */
  uid_t   uid;
  gid_t   gid;
  int     fd;                  /* listening socket, -1 if not open */
  char   *prog;                /* program path */
  char   *argv[MAXARGS + 1];   /* exec argv, NULL-terminated */
  int     builtin;             /* index into builtins[], -1 if external */
};

/* ---- built-in services ---- */
struct builtin {
  char name[8];
  void (*stream_fn)(int fd);
  void (*dgram_fn)(int fd);
};

static void bi_echo_stream(int fd)
{
  char buf[512];
  int n;
  while ((n = read(fd, buf, sizeof(buf))) > 0)
    writeall(fd, buf, n);
}

static void bi_echo_dgram(int fd)
{
  char buf[65536];
  union socksaddr sa;
  socklen_t sl = sizeof(sa);
  int n = recvfrom(fd, buf, sizeof(buf), MSG_DONTWAIT, &sa.s, &sl);
  if (n > 0) sendto(fd, buf, n, 0, &sa.s, sl);
}

static void bi_discard_stream(int fd)
{
  char buf[512];
  while (read(fd, buf, sizeof(buf)) > 0) ;
}

static void bi_discard_dgram(int fd)
{
  char buf[512];
  recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
}

static void bi_time_stream(int fd)
{
  uint32_t t = htonl((uint32_t)(time(NULL) + 2208988800UL));
  writeall(fd, &t, 4);
}

static void bi_time_dgram(int fd)
{
  char buf[4];
  union socksaddr sa;
  socklen_t sl = sizeof(sa);
  uint32_t t;
  if (recvfrom(fd, buf, sizeof(buf), MSG_DONTWAIT, &sa.s, &sl) < 0) return;
  t = htonl((uint32_t)(time(NULL) + 2208988800UL));
  sendto(fd, &t, 4, 0, &sa.s, sl);
}

static void bi_daytime_stream(int fd)
{
  time_t t = time(NULL);
  char *s = ctime(&t);
  dprintf(fd, "%.24s\r\n", s);
}

static void bi_daytime_dgram(int fd)
{
  char buf[4];
  union socksaddr sa;
  socklen_t sl = sizeof(sa);
  time_t t;
  if (recvfrom(fd, buf, sizeof(buf), MSG_DONTWAIT, &sa.s, &sl) < 0) return;
  t = time(NULL);
  char tbuf[32];
  snprintf(tbuf, sizeof(tbuf), "%.24s\r\n", ctime(&t));
  sendto(fd, tbuf, strlen(tbuf), 0, &sa.s, sl);
}

static void bi_chargen_stream(int fd)
{
  int i, j;
  char line[74];
  for (i = 0; ; i++) {
    for (j = 0; j < 72; j++)
      line[j] = ' ' + (i + j) % 95;
    line[72] = '\r'; line[73] = '\n';
    if (writeall(fd, line, 74) < 0) break;
  }
}

static void bi_chargen_dgram(int fd)
{
  char buf[4], line[74];
  union socksaddr sa;
  socklen_t sl = sizeof(sa);
  int j;
  static int pos;
  if (recvfrom(fd, buf, sizeof(buf), MSG_DONTWAIT, &sa.s, &sl) < 0) return;
  for (j = 0; j < 72; j++)
    line[j] = ' ' + (pos + j) % 95;
  line[72] = '\r'; line[73] = '\n';
  sendto(fd, line, 74, 0, &sa.s, sl);
  pos = (pos + 1) % 95;
}

static const struct builtin builtins[] = {
  { "echo",    bi_echo_stream,    bi_echo_dgram    },
  { "discard", bi_discard_stream, bi_discard_dgram },
  { "time",    bi_time_stream,    bi_time_dgram    },
  { "daytime", bi_daytime_stream, bi_daytime_dgram },
  { "chargen", bi_chargen_stream, bi_chargen_dgram },
};

/* ---- fd-set helpers ---- */
static void fd_add(int fd)
{
  if (fd < 0) return;
  FD_SET(fd, &TT.rfds);
  if (fd > TT.maxfd) TT.maxfd = fd;
}

static void fd_del(int fd)
{
  if (fd < 0) return;
  FD_CLR(fd, &TT.rfds);
  /* recompute maxfd lazily on next select */
  if (fd == TT.maxfd) {
    while (TT.maxfd > 0 && !FD_ISSET(TT.maxfd, &TT.rfds))
      TT.maxfd--;
  }
}

/* ---- socket open/close ---- */
static void open_svc(struct servtab *s)
{
  struct addrinfo hints, *res, *rp;
  const char *host;
  int fd, one = 1, rc;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = s->family;
  hints.ai_socktype = s->socktype;
  hints.ai_flags    = AI_PASSIVE;

  host = s->host ? s->host : TT.defhost;
  if (!host || !strcmp(host, "*")) host = NULL;

  rc = getaddrinfo(host, s->service, &hints, &res);
  if (rc) {
    loggit(LOG_ERR, "getaddrinfo %s: %s", s->service, gai_strerror(rc));
    TT.alarm_armed = 1;
    alarm(RETRYTIME);
    return;
  }

  for (fd = -1, rp = res; rp; rp = rp->ai_next) {
    fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (fd < 0) continue;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (!bind(fd, rp->ai_addr, rp->ai_addrlen)) break;
    close(fd); fd = -1;
  }
  freeaddrinfo(res);

  if (fd < 0) {
    loggit(LOG_ERR, "%s/%s: bind: %s", s->service, s->proto, strerror(errno));
    TT.alarm_armed = 1;
    alarm(RETRYTIME);
    return;
  }
  if (s->socktype == SOCK_STREAM) listen(fd, TT.q);
  s->fd = fd;
  fd_add(fd);
}

static void close_svc(struct servtab *s)
{
  if (s->fd < 0) return;
  fd_del(s->fd);
  close(s->fd);
  s->fd = -1;
}

/* ---- config parsing ---- */

/* free all fields of a servtab (not the struct itself) */
static void free_svc(struct servtab *s)
{
  int i;
  free(s->host); free(s->service); free(s->proto); free(s->prog);
  for (i = 0; i <= MAXARGS; i++) { free(s->argv[i]); s->argv[i] = NULL; }
}

/* return index in builtins[] or -1 */
static int find_builtin(const char *name)
{
  unsigned i;
  for (i = 0; i < ARRAY_LEN(builtins); i++)
    if (!strcmp(builtins[i].name, name)) return i;
  return -1;
}

static void load_config(void)
{
  FILE *fp;
  char *line;
  const char *conf = toys.optc ? toys.optargs[0] : "/etc/inetd.conf";
  char *defhost = xstrdup("*");

  /* close and remove all existing services */
  while (TT.svcs) {
    struct servtab *s = TT.svcs;
    TT.svcs = s->next;
    close_svc(s);
    free_svc(s);
    free(s);
  }
  FD_ZERO(&TT.rfds);
  TT.maxfd = 0;

  fp = fopen(conf, "r");
  if (!fp) { perror_msg("%s", conf); free(defhost); return; }

  while ((line = xgetline(fp)) != NULL) {
    char *tok[6 + MAXARGS], *p = line;
    int ntok, i;
    struct servtab *s;

    /* skip blanks and comments */
    while (*p == ' ' || *p == '\t') p++;
    if (!*p || *p == '#') { free(line); continue; }

    /* tokenize */
    ntok = 0;
    while (*p && ntok < (int)ARRAY_LEN(tok)) {
      tok[ntok++] = p;
      while (*p && *p != ' ' && *p != '\t') p++;
      if (*p) *p++ = '\0';
      while (*p == ' ' || *p == '\t') p++;
    }

    /* handle "host:" default-host line (exactly 1 token ending with ':') */
    if (ntok == 1 && tok[0][strlen(tok[0])-1] == ':') {
      free(defhost);
      char *tmp = xstrdup(tok[0]);
      tmp[strlen(tmp)-1] = '\0';
      defhost = tmp;
      free(line); continue;
    }

    if (ntok < 6) {
      loggit(LOG_ERR, "%s: too few fields", conf);
      free(line); continue;
    }

    s = xzalloc(sizeof(*s));
    s->fd = -1;
    s->builtin = -1;
    s->max = TT.R;

    /* field 0: [host:]service */
    {
      char *colon = strrchr(tok[0], ':');
      if (colon) {
        s->host = xstrndup(tok[0], colon - tok[0]);
        s->service = xstrdup(colon + 1);
      } else {
        s->host = xstrdup(defhost);
        s->service = xstrdup(tok[0]);
      }
    }

    /* field 1: stream|dgram */
    if (!strcmp(tok[1], "stream"))     s->socktype = SOCK_STREAM;
    else if (!strcmp(tok[1], "dgram")) s->socktype = SOCK_DGRAM;
    else {
      loggit(LOG_ERR, "%s: bad socket type '%s'", s->service, tok[1]);
      goto bad;
    }

    /* field 2: tcp[6] | udp[6] */
    s->proto = xstrdup(tok[2]);
    {
      char *base = tok[2];
      int len = strlen(base);
      s->family = AF_INET;
      if (len > 0 && base[len-1] == '6') {
        s->family = AF_INET6;
        /* strip '6' for proto_no check below */
        base = xstrndup(base, len-1);
      } else base = xstrdup(base);
      if (strcmp(base, "tcp") && strcmp(base, "udp")) {
        loggit(LOG_ERR, "%s: bad protocol '%s'", s->service, tok[2]);
        free(base); goto bad;
      }
      free(base);
    }

    /* field 3: wait|nowait[.max] */
    {
      char *dot = strchr(tok[3], '.');
      if (dot) { *dot++ = '\0'; s->max = (unsigned)atol(dot); }
      if (!strncmp(tok[3], "nowait", 6))     s->wait = 0;
      else if (!strncmp(tok[3], "wait", 4))  s->wait = 1;
      else { loggit(LOG_ERR, "%s: bad wait field", s->service); goto bad; }
    }

    /* field 4: user[.group] or user[:group] */
    {
      char *grp, *tmp = xstrdup(tok[4]);
      struct passwd *pw;
      struct group *gr;
      grp = strchr(tmp, '.'); if (!grp) grp = strchr(tmp, ':');
      if (grp) *grp++ = '\0';
      pw = getpwnam(tmp);
      if (!pw) { loggit(LOG_ERR, "%s: unknown user '%s'", s->service, tmp); free(tmp); goto bad; }
      s->uid = pw->pw_uid; s->gid = pw->pw_gid;
      if (grp && *grp) {
        gr = getgrnam(grp);
        if (!gr) { loggit(LOG_ERR, "%s: unknown group '%s'", s->service, grp); free(tmp); goto bad; }
        s->gid = gr->gr_gid;
      }
      free(tmp);
    }

    /* field 5: program path (or "internal") */
    s->prog = xstrdup(tok[5]);
    if (!strcmp(s->prog, "internal")) {
      s->builtin = find_builtin(s->service);
      if (s->builtin < 0) {
        loggit(LOG_ERR, "%s: unknown builtin service", s->service);
        goto bad;
      }
    }

    /* remaining fields: argv (copy tok[5] as argv[0] if none) */
    for (i = 0; i < MAXARGS && 6+i < ntok; i++)
      s->argv[i] = xstrdup(tok[6+i]);
    if (i == 0) s->argv[0] = xstrdup(s->prog);
    s->argv[i] = NULL;

    /* link into list */
    s->next = TT.svcs;
    TT.svcs = s;

    /* open socket */
    open_svc(s);
    free(line);
    continue;
bad:
    free_svc(s); free(s);
    free(line);
  }
  fclose(fp);
  free(defhost);
}

/* ---- signal handlers ---- */
/* SIGHUP: set flag, main loop will call load_config() */
static volatile sig_atomic_t need_reload;
static void sighup_handler(int sig)
{
  (void)sig;
  need_reload = 1;
}

static void sigalrm_handler(int sig)
{
  struct servtab *s;
  int save = errno;
  (void)sig;
  TT.alarm_armed = 0;
  for (s = TT.svcs; s; s = s->next)
    if (s->fd < 0) open_svc(s);
  errno = save;
}

static void sigchld_handler(int sig)
{
  int status, save = errno;
  pid_t pid;
  struct servtab *s;
  (void)sig;
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    for (s = TT.svcs; s; s = s->next) {
      if (s->wait != (int)pid) continue;
      s->wait = 1;      /* re-arm wait service */
      fd_add(s->fd);
      break;
    }
  }
  errno = save;
}

static void sigterm_handler(int sig)
{
  struct servtab *s;
  (void)sig;
  for (s = TT.svcs; s; s = s->next) close_svc(s);
  unlink("/var/run/inetd.pid");
  _exit(0);
}

/* ---- child: exec or run builtin ---- */
static void do_child(struct servtab *s, int connfd)
{
  struct servtab *s2;

  /* drop privileges */
  if (s->gid != getgid()) setgid(s->gid);
  if (s->uid != getuid()) setuid(s->uid);

  /* redirect connection to stdin/stdout/stderr */
  dup2(connfd, 0);
  dup2(connfd, 1);
  if (!s->wait) dup2(connfd, 2);

  /* close all other service fds */
  for (s2 = TT.svcs; s2; s2 = s2->next)
    if (s2->fd != connfd) { close(s2->fd); s2->fd = -1; }
  if (connfd > 2) close(connfd);

  setsid();

  if (s->builtin >= 0) {
    if (s->socktype == SOCK_STREAM)
      builtins[s->builtin].stream_fn(0);
    else
      builtins[s->builtin].dgram_fn(0);
    _exit(0);
  }

  execv(s->prog, s->argv);
  perror_msg("exec %s", s->prog);
  _exit(127);
}

/* ---- main ---- */
void inetd_main(void)
{
  FD_ZERO(&TT.rfds);
  TT.maxfd = 0;
  TT.defhost = xstrdup("*");

  load_config();

  if (!TT.svcs) error_exit("no services configured");

  /* daemonize unless -f */
  if (!FLAG(f)) xvdaemon();

  xpidfile("inetd");

  /* set up signal handlers */
  xsignal(SIGHUP,  sighup_handler);
  xsignal(SIGALRM, sigalrm_handler);
  xsignal(SIGCHLD, sigchld_handler);
  xsignal(SIGTERM, sigterm_handler);
  xsignal(SIGINT,  sigterm_handler);
  xsignal(SIGPIPE, SIG_IGN);

  if (!FLAG(e))
    openlog("inetd", LOG_PID | LOG_NDELAY, LOG_DAEMON);

  for (;;) {
    fd_set rdup;
    int nready;
    struct servtab *s;

    /* SIGHUP: reload config */
    if (need_reload) {
      need_reload = 0;
      load_config();
    }

    rdup = TT.rfds;
    nready = select(TT.maxfd + 1, &rdup, NULL, NULL, NULL);
    if (nready < 0) {
      if (errno == EINTR) continue;
      perror_exit("select");
    }

    for (s = TT.svcs; nready > 0 && s; s = s->next) {
      int connfd, new_udp_fd = -1;
      pid_t pid;

      if (s->fd < 0 || !FD_ISSET(s->fd, &rdup)) continue;
      nready--;

      connfd = s->fd;

      if (!s->wait) {
        if (s->socktype == SOCK_STREAM) {
          connfd = accept(s->fd, NULL, NULL);
          if (connfd < 0) {
            if (errno != EINTR && errno != EAGAIN) perror_msg("accept");
            continue;
          }
        } else {
          /* UDP nowait: create new socket for parent to avoid
           * the connected-socket sharing problem after fork */
          struct addrinfo hints, *res;
          int one = 1;
          const char *host = s->host ? s->host : TT.defhost;
          if (!strcmp(host, "*")) host = NULL;
          memset(&hints, 0, sizeof(hints));
          hints.ai_family   = s->family;
          hints.ai_socktype = SOCK_DGRAM;
          hints.ai_flags    = AI_PASSIVE;
          if (!getaddrinfo(host, s->service, &hints, &res)) {
            new_udp_fd = socket(res->ai_family, SOCK_DGRAM, 0);
            if (new_udp_fd >= 0) {
              setsockopt(new_udp_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
              if (bind(new_udp_fd, res->ai_addr, res->ai_addrlen)) {
                close(new_udp_fd); new_udp_fd = -1;
              }
            }
            freeaddrinfo(res);
          }
          if (new_udp_fd < 0) {
            /* eat the packet and skip */
            char tmp[1];
            recv(s->fd, tmp, 1, MSG_DONTWAIT);
            continue;
          }
        }
      }

      /* rate limiting */
      if (s->max > 0) {
        unsigned now = (unsigned)time(NULL);
        if (++s->count == 1) s->setime = now;
        else if (s->count >= s->max) {
          if (now - s->setime <= CNT_INTERVAL) {
            loggit(LOG_ERR, "%s: too many connections, pausing", s->service);
            close_svc(s);
            s->count = 0;
            if (!TT.alarm_armed) { TT.alarm_armed = 1; alarm(RETRYTIME); }
            if (new_udp_fd >= 0) close(new_udp_fd);
            if (connfd != s->fd) close(connfd);
            continue;
          }
          s->count = 0;
        }
      }

      /* fork */
      pid = xfork();
      if (!pid) {
        /* child: close new_udp_fd we don't need */
        if (new_udp_fd >= 0) close(new_udp_fd);
        do_child(s, connfd);
        /* do_child never returns */
      }

      /* parent */
      if (s->wait) {
        s->wait = pid;
        fd_del(s->fd);
      }
      if (new_udp_fd >= 0) {
        /* swap in the new socket so parent listens on a fresh fd */
        fd_del(s->fd);
        close(s->fd);
        s->fd = new_udp_fd;
        fd_add(s->fd);
      }
      if (connfd != s->fd) close(connfd);
    }
  }
}
