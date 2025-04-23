#include "server.h"
#include "PoolItemListen.h"


CPoolItemListen::CPoolItemListen(int port) : CPoolItem() {
	int r = create(port);
	printf("r = %d\n", r);
}

CPoolItemListen::~CPoolItemListen() {}

bool CPoolItemListen::create(int port) {
	struct sockaddr_in addr;
	int true_value = 1;
	

	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	printf("listen sock = %d\n", sock);
	if (sock < 0) return false;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&true_value, sizeof(true_value));

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = 0;

	if (bind(sock, (sockaddr*)&addr, sizeof(addr))) return false;
	if (listen(sock, 10)) return false;
	SetNonBlock(sock);
	return true;
}

bool CPoolItemListen::evRead(){
	int inSock;
	struct sockaddr_in addr;
	socklen_t len;
	CPoolItemClient *cli;

	while (true) {
		len = sizeof(addr);
		inSock = accept(sock, (sockaddr*)&addr, &len);
		printf("inSock = %d\n", inSock);
		if (inSock < 0) return true;

		SetNonBlock(inSock);
		cli = new CPoolItemClient(inSock);
		addToPool(cli, EPOLLIN|EPOLLOUT);
		addToList(cli);
	}
	
	return true;
}

bool CPoolItemListen::evWrite(){
	return true;
}

void CPoolItemListen::sendPacket(int type, const void *buf, int size) {
	return;
}
