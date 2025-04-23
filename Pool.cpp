#include "server.h"
#include "Pool.h"

CPool::CPool() {
}

CPool::~CPool() {
}

bool CPool::create() {
	return (fdPool = epoll_create(16)) >= 0;
}

void CPool::add(CPoolItem *item, __uint32_t ev) {
	struct epoll_event ep_ev;

	ep_ev.events = ev|EPOLLET;
	ep_ev.data.ptr = item;

	int r = epoll_ctl(fdPool, EPOLL_CTL_ADD, item->getSock(), &ep_ev);

	printf("epoll_ctl = %d sock = %d\n", r, item->getSock());
	perror("epoll_ctl");
}

void CPool::remove(CPoolItem *item) {
	printf("remove socket = %d\n", item->getSock());
	epoll_ctl(fdPool, EPOLL_CTL_DEL, item->getSock(), 0);
	removeFromList((CPoolItemClient*)item);

	delete item;
}

void CPool::sendPacketsToAll(std::map<int, int> ids, int type, void *body, int size) {
	for (auto iter = items.begin(); iter != items.end(); iter++) {
		if ((*iter)->getUserId() && ids.count((*iter)->getUserId())) {
			(*iter)->sendPacket(type, body, size);
		}
	}
}

void CPool::wait() {
	struct epoll_event ep_ev[64];
	CPoolItem *itm;
	int i;

	int r = epoll_wait(fdPool, ep_ev, sizeof(ep_ev)/sizeof(ep_ev[0]), 2000);
	if (r) printf("epoll_wait = %d\n", r);

	for (i = 0; i < r; i++) {
		itm = (CPoolItem*)ep_ev[i].data.ptr;
		printf("itm sock = %d %p\n", itm->getSock(), itm);
		if (ep_ev[i].events & EPOLLIN) itm->evRead();
		if (itm->getPendingClose() && itm->getOutBufSize() == 0) {
			remove(itm);
			continue;
		}

		if (ep_ev[i].events & EPOLLOUT) itm->evWrite();
		if (itm->getPendingClose() && itm->getOutBufSize() == 0) remove(itm);
	}
}