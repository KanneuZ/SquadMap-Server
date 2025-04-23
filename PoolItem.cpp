#include "server.h"

CPoolItem::CPoolItem() {
	// CPoolItem(-1);
	sock = -1;
}

CPoolItem::CPoolItem(int sk) {
	sock = sk;
}


CPoolItem::~CPoolItem() {
	close();
}

void CPoolItem::close() {
	if (sock >= 0) ::close(sock);
	sock = -1;
}