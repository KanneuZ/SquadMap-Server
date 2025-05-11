#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

int sock;

/*функция отправляет пакет с заголовком на сервер.*/
void sendPacket(int type, const void *data, int size) {
	char buf[65536];
	int r;

	buf[0] = (char)type;
	*(unsigned short*)(buf+1) = size+3;
	memcpy(buf+3, data, size);

	size += 3;
	r = send(sock, buf, size, 0);
	printf("send %d/%d\n", r, size);
}

int main() {	
	struct sockaddr_in addr;
	int inDataSize, r, type, size;
	char buf[65536];
	char str[] = "andr2\x00""1\x00Kenny";
	
	/*создание сокета.*/
	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	inDataSize = 0;

	/*насторойка адреса.*/
	addr.sin_family = AF_INET;
	inet_aton("127.0.0.1", &addr.sin_addr);
	addr.sin_port = htons(4000);

	/*подключение к серверу и сразу проверка.*/
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr))) {
		printf("cli connect error\n");
		return 1;
	}

	sendPacket(2, str, sizeof(str)-1);

	/*прием данных с сервера и удаление ненужного пакета из буфера.*/
	while (true) {
		r = recv(sock, buf+inDataSize, sizeof(buf)-inDataSize, 0);
		printf("recv(%d) = %d\n", sock, r);
		if (r <= 0) break;

		// type size body

		inDataSize += r;
		while (inDataSize >= 3) {
			type = (unsigned)buf[0];
			size = *(unsigned short*)(buf+1);

			if (inDataSize < size) break;

			printf("t = %d size = %d\n", type, size);
			inDataSize -= size;
			if (inDataSize) memmove(buf, buf + size, inDataSize);
		}
	}

	close(sock);
	return 0;
}
