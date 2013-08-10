#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <utime.h>


#include <mysql.h>
#include <mysqld_error.h>
#include <signal.h>
#include <stdarg.h>
#include <sslopt-vars.h>
#include <assert.h>

#define WWW_ROOT "/var/www/html"
// TILE_PATH must have tile z directory z(0..18)/x/y.png
#define TILE_PATH "/osm_tiles2"


// Build parent directories for the specified file name
// Note: the part following the trailing / is ignored
// e.g. mkdirp("/a/b/foo.png") == shell mkdir -p /a/b
static int mkdirp(const char *path) {
    struct stat s;
    char tmp[PATH_MAX];
    char *p;

    strncpy(tmp, path, sizeof(tmp));

    // Look for parent directory
    p = strrchr(tmp, '/');
    if (!p)
        return 0;

    *p = '\0';

    if (!stat(tmp, &s))
        return !S_ISDIR(s.st_mode);
    *p = '/';
    // Walk up the path making sure each element is a directory
    p = tmp;
    if (!*p)
        return 0;
    p++; // Ignore leading /
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            if (!stat(tmp, &s)) {
                if (!S_ISDIR(s.st_mode))
                    return 1;
            } else if (mkdir(tmp, 0777))
                return 1;
            *p = '/';
        }
        p++;
    }
    return 0;
}

void parseDate(struct tm *tm, const char *str)
{
    // 2007-05-20 13:51:35
    bzero(tm, sizeof(*tm));
    int n = sscanf(str, "%d-%d-%d %d:%d:%d",
       &tm->tm_year, &tm->tm_mon, &tm->tm_mday, &tm->tm_hour, &tm->tm_min, &tm->tm_sec);

    if (n !=6)
        printf("failed to parse date string, got(%d): %s\n", n, str);
 
    tm->tm_year -= 1900;
}

int main(int argc, char **argv)
{
  MYSQL mysql;
  char query[255];
  MYSQL_RES *res;
  MYSQL_ROW row;
  mysql_init(&mysql);

  if (!(mysql_real_connect(&mysql,"","tile","tile","tile",MYSQL_PORT,NULL,0)))
  {
    fprintf(stderr,"%s: %s\n",argv[0],mysql_error(&mysql));
    exit(1);
  }
  mysql.reconnect= 1;

  snprintf(query, sizeof(query), "SELECT x,y,z,data,created_at FROM tiles");

  if ((mysql_query(&mysql, query)) || !(res= mysql_use_result(&mysql)))
  {
    fprintf(stderr,"Cannot query tiles: %s\n", mysql_error(&mysql));
    exit(1);
  }

  while ((row= mysql_fetch_row(res)))
  {
      ulong *lengths= mysql_fetch_lengths(res);
      char path[PATH_MAX];
      unsigned long int x,y,z,length;
      time_t created_at;
      const char *data;
      struct tm date;
      int fd;
      struct utimbuf utb;

      assert(mysql_num_fields(res) == 5);

      //printf("x(%s) y(%s) z(%s) data_length(%lu): %s\n", row[0], row[1], row[2], lengths[3], row[4]);

      x = strtoul(row[0], NULL, 10);
      y = strtoul(row[1], NULL, 10);
      z = strtoul(row[2], NULL, 10);
      data = row[3];
      length = lengths[3];
      parseDate(&date, row[4]);
      created_at = mktime(&date);

      //printf("x(%lu) y(%lu) z(%lu) data_length(%lu): %s", x,y,z,length,ctime(&created_at));

      if (!length) {
          printf("skipping empty tile x(%lu) y(%lu) z(%lu) data_length(%lu): %s", x,y,z,length,ctime(&created_at));
          continue;
      }

      snprintf(path, PATH_MAX, WWW_ROOT TILE_PATH "/%lu/%lu/%lu.png", z, x, y);
      printf("%s\n", path);
      mkdirp(path);

      fd = open(path, O_CREAT | O_WRONLY, 0644);
      if (fd <0) {
          perror(path);
          exit(1);
      }
      if (write(fd, data, length) != length) {
          perror("writing tile");
          exit(2);
      }
      close(fd);
      utb.actime  = created_at;
      utb.modtime = created_at;
      if (utime(path, &utb) < 0) {
          perror("utime");
          exit(3);
      }
  }

  printf ("Number of rows: %lu\n", (unsigned long) mysql_num_rows(res));
  mysql_free_result(res);

  mysql_close(&mysql);	/* Close & free connection */
  return 0;
}
