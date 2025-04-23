#include "server.h"

CPool pool;
PGconn *dbconn=0;

int main(int argc, char const *argv[]) {
	dbconn=PQconnectdb(PGLOGIN);
	if (PQstatus(dbconn) != CONNECTION_OK) {
		printf("Ошибка подключения к БД\n");
		return 1;
	}

	signal(SIGPIPE, SIG_IGN);

	pool.create();

	CPoolItemListen *lst = new CPoolItemListen(SERVER_PORT);
	pool.add(lst, EPOLLIN);

	while(true) pool.wait();

	if (dbconn) PQfinish(dbconn);
	return 0;
}

void addToPool(CPoolItem *item, __uint32_t ev) {
	pool.add(item, ev);
}

void addToList(CPoolItemClient *item) {
	pool.addToList(item);
}

void sendToAll(std::map<int, int> ids, int type, void *body, int size) {
	pool.sendPacketsToAll(ids, type, body, size);
}

void SetNonBlock(int fd) {
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0)|O_NONBLOCK);
}

void dmp(const void *buf, int len) {
  int i, b=0;
  char asc[17];
  unsigned char c;
  char tmpbuf[128], *p;

  if (buf==0) {
    printf("!!! buf=0!\n");
    return;
  }

  while (b<len) {
    p=tmpbuf+sprintf(tmpbuf, "%04X: ", b);
  	asc[16]=0;
    for (i=0; i<16; i++, b++) {
      if (b<len) {
        c=((unsigned char *)buf)[b];
        p+=sprintf(p, "%02X ", c);
        asc[i]=(c<0x20) ? '.' : c;
      } else {p+=sprintf(p, "   "); asc[i]=' ';}
      if (i==7) p+=sprintf(p, "| ");
    }
    printf("%s %s\n", tmpbuf, asc);
  }
}


PGresult *dbexec(const char *req) {
	if (!dbconn) return 0;	
	return PQexec(dbconn, req);
}

PGresult *dbexecf(const char *fmt, ...) {
	char buf[32768];
	va_list argptr;

	if (!dbconn) return 0;

	va_start(argptr, fmt);
	vsnprintf(buf, sizeof(buf), fmt, argptr);
	va_end(argptr);

	return dbexec(buf);
}

char *dbescape(const char *str) {
	return PQescapeLiteral(dbconn, str, strlen(str));
}

const char *dberror() {
	return PQerrorMessage(dbconn);
}

// PQresultStatus(res) == PGRES_TUPLES_OK
// PQclear(res)
// PQnfields(res)
// PQntuples(res)
// PQerrorMessage(dbconn)
// i - PQntuples(res)
// j - PQnfields(res)
// PQgetvalue(res, i,j)

// PQescapeLiteral(dbconn, str, strlen(str))
// const char *PQerrorMessage(dbconn);