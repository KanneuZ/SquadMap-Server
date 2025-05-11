#include "server.h"

/*маски для быстрого изменения.*/
#define GROUP_MASK_CHANGE_NAME	 1
#define GROUP_MASK_CHANGE_LEAD	 2
#define GROUP_MASK_CHANGE_STAT	 4
#define GROUP_MASK_MAKE_PRIM	 8

#define MEMBER_MASK_CHANGE_NAME_OR_DELL		 1
#define MEMBER_MASK_CHANGE_ONLINE 	 		 2
#define MEMBER_MASK_CHANGE_MAKE_LEAD 		 4
#define MEMBER_MASK_CHANGE_STAT	 			 8

/*конструктор.*/
CPoolItemClient::CPoolItemClient(int sk) : CPoolItem(sk) {
	inDataSize = 0;
	userId = 0;
	fPendingClose = false;
}

/*деконструктор.*/
CPoolItemClient::~CPoolItemClient() {
}

/*обработка входящих пакетов.*/
bool CPoolItemClient::evRead() {
	int size, type, r;

	while (true) {
		r = recv(sock, buf+inDataSize, IN_BUFF_SIZE-inDataSize, 0);
		printf("recv(%d) = %d\n", sock, r);
		if (r < 0) {
			if (errno == EAGAIN) return true;
			return false;
		}

		if (r == 0) {
			inDataSize = 0;
			outBuf.clear();
			pendingClose();
			return false;
		}

		// type size body

		inDataSize += r;
		while (inDataSize >= 3) {
			type = (unsigned)buf[0];
			size = *(unsigned short*)(buf+1);

			if (inDataSize < size) break;

			parsePacket(type, size-3);
			inDataSize -= size;
			if (inDataSize) memmove(buf, buf + size, inDataSize);
		}
	}

	return true;
}

/*обработка для записи данных. после взятия из буфера данные удаляются.*/
bool CPoolItemClient::evWrite() {
	int r, offset, size;

	size = outBuf.size();
	for (offset = 0; offset < size; offset += r) {
		r = send(sock, outBuf.data() + offset, size - offset, 0);
		if (r <= 0) break;
	}

	if (offset < size) outBuf.erase(outBuf.begin(), outBuf.begin()+offset);
	return true;
}

/*анализ пакета???*/
bool CPoolItemClient::parsePacket(int type, int size) {
	char *body = buf+3;

	printf("parsePacket = %d s = %d\n", type, size);
	dmp(body, size);

	switch (type) {
		case PKT_USER_REG:      	 return PKTUserReg(body, size);
		case PKT_USER_LOGIN:    	 return PKTUserLogin(body, size);
		case PKT_GROUP_CREATE:  	 return PKTGroupCreate(body, size);
		case PKT_GROUP_JOIN:    	 return PKTGroupJoin(body, size);
		case PKT_USER_COORDS:   	 return PKTSendCoordsToAll(body, size);
		case PKT_MARKER_CREATE: 	 return PKTAddMarker(body, size);
		case PKT_GROUP_CHANGE_LEAD:  return PKTGroupChangeLead(body, size);
		case PKT_USER_CHANGE_PRIMGR: return PKTChangePrimGr(body, size);
		case PKT_GROUP_EXIT:		 return PTKExitGr(body, size);
		case PKT_GROUP_KICK:		 return PKTGroupKick(body, size);
		case PKT_MARKER_REM:		 return PKTDeleteMarker(body, size);
	}

	return true;
}

/*отправка пакета клиенту.*/
void CPoolItemClient::sendPacket(int type, const void *buf, int size) {
	int offset, i, r;
	char buf2[65536];

	memcpy(buf2+3, buf, size);

	size+=3;
	buf2[0] = type;
	*(unsigned short*)(buf2+1) = size;

	if (outBuf.size()) {
		for (i = 0; i < size; i++) outBuf.push_back(((char*)buf2)[i]);
		return;
	}

	for (offset = 0; offset < size; offset += r) {
		printf("before send sock=%d\n", sock);
		r = send(sock, buf2+offset, size - offset, 0);
		printf("after send sock=%d r=%d\n", sock, r);
		if (r <= 0) break;
	}

	for (i = offset; i < size; i++) outBuf.push_back(((char*)buf2)[i]);
	// if (errno == EAGAIN) return;
}

/*чтение данных из пакета.*/
bool readString(std::string &str, const char *&body, const char *end) {
	const char *st = body;
	if (body >= end) return false;

	while (*body && body < end) body++;
	str = std::string(st, body - st);
	if (body != end) body++;

	return true;
}

/*почему рид аррей какое тут блтьб чтение массива и где*/
bool readArray(std::vector<int> &vec, char *arr) {
	char *ptr;

	if (strlen(arr) < 3) return false;

	arr[strlen(arr)-1] = 0;
	arr++;

	ptr = arr;
	do {
		arr = ptr;
		ptr=strchr(arr, ',');
		if ((ptr=strchr(arr, ','))) *ptr++=0;
		vec.push_back(atoi(arr));
		printf("%d\n", atoi(arr));
	} while(ptr);

	return true;
}

/*обработка ошибок для регистрации и отправка результата к клиенту.*/
bool CPoolItemClient::PKTUserReg(const char *body, int size) {
	std::string login, password, name;
	char error1[] = "\x01Неправильный пакет.";
	char error2[] = "\x02Такой пользователь существует.";
	char *plog, *ppass, *pname;
	PGresult *res;
	const char *end = body + size;

	if (!readString(login, body, end) || !readString(password, body, end) || !readString(name, body, end)) {
		sendPacket(PKT_USER_REG, error1, sizeof(error1)-1);
		pendingClose();
		return false;
	}

	plog = dbescape(login.c_str());
	ppass = dbescape(password.c_str());
	pname = dbescape(name.c_str());

	res = dbexecf("INSERT INTO USERS (login, password, name) VALUES (%s, %s, %s) RETURNING id", plog, ppass, pname);
	PQfreemem(plog);
	PQfreemem(ppass);
	PQfreemem(pname);

	printf("%s\n", dberror());
	if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) < 1) {
		sendPacket(PKT_USER_REG, error2, sizeof(error2)-1);
		pendingClose();
		PQclear(res);
		return false;
	}

	userId = atoi(PQgetvalue(res, 0, 0));
	nickName = name;

	sendPacket(PKT_USER_REG, "\x00", 1);
	PQclear(res);
	return true;
}

/*обработка ошибок для входа пользователя и отправка полученной информации к клиенту.*/
bool CPoolItemClient::PKTUserLogin(const char *body, int size) {
	std::string login, password;
	char error1[] = "\x01Неправильный пакет.";
	char error2[] = "\x02Неверный логин или пароль.";
	char *plog, *ppass, *packetEnd, *name, *packetBuf;
	int len;
	PGresult *res;
	const char *end = body + size;

	if (!readString(login, body, end) || !readString(password, body, end)) {
		sendPacket(PKT_USER_LOGIN, error1, sizeof(error1)-1);
		pendingClose();
		return false;
	}

	plog = dbescape(login.c_str());
	ppass = dbescape(password.c_str());

	printf("PKTUserLogin %d %s %s\n", size, login.c_str(), password.c_str());

	res = dbexecf("SELECT id, name, primgrid FROM users WHERE login=%s AND password=%s", plog, ppass);
	PQfreemem(plog);
	PQfreemem(ppass);

	if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) < 1) {
		sendPacket(PKT_USER_LOGIN, error2, sizeof(error2)-1);
		pendingClose();
		PQclear(res);
		return false;
	}


	userId = atoi(PQgetvalue(res, 0, 0));
	nickName = name = PQgetvalue(res, 0, 1);
	primGr = atoi(PQgetvalue(res, 0, 2));

	len = strlen(name);
	packetBuf = new char[len+9];
	packetEnd = packetBuf;

	*packetEnd++ = 0;
	*(int*)packetEnd= userId;
	packetEnd += 4;
	*(int*)packetEnd = primGr;
	packetEnd += 4;
	memcpy(packetEnd, name, len);
	packetEnd += len;

	sendPacket(PKT_USER_LOGIN, packetBuf, packetEnd-packetBuf);
	delete[] packetBuf;

	PKTGroupInfo();
	PKTMarkersInfo();
	PQclear(res);
	return true;
}

/*создание группы и обработка ошибок.*/
bool CPoolItemClient::PKTGroupCreate(const char *body, int size) {
	std::string name, password;
	char error1[] = "\x01Неправильный пакет.";
	char error2[] = "\x02Группа с таким названием уже существует.";
	char *pname, *ppass;
	int grId;
	PGresult *res;
	const char *end = body + size;

	if (!readString(name, body, end) || !readString(password, body, end)) {
		sendPacket(PKT_GROUP_CREATE, error1, sizeof(error1)-1);
		pendingClose();
		return false;
	}

	pname = dbescape(name.c_str());
	ppass = dbescape(password.c_str());

	res = dbexecf("INSERT INTO GROUPS (name, password, lead) VALUES (%s, %s, %d) RETURNING id", pname, ppass, userId);
	PQfreemem(pname);
	PQfreemem(ppass);

	if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) < 1) {
		sendPacket(PKT_GROUP_CREATE, error2, sizeof(error2)-1);
		pendingClose();
		PQclear(res);
		return false;
	}

	grId = atoi(PQgetvalue(res, 0, 0));
	res = dbexecf("UPDATE users SET goroups = (goroups || %d) WHERE id=%d AND (NOT ('{%d}' <@ goroups) OR goroups IS NULL)", grId, userId, grId);

	sendPacket(PKT_GROUP_CREATE, "\x00", 1);
	PKTSingleGroupUpdate(grId);
	PQclear(res);
	return true;
}

/*вход в группу и обработка ошибок.*/
bool CPoolItemClient::PKTGroupJoin(const char *body, int size) {
	std::string name, password;
	char error1[] = "\x01Неправильный пакет.";
	char error2[] = "\x02Неверное имя группы или пароль";
	char *pname, *ppass;
	int grId, leadId;
	PGresult *res;
	const char *end = body + size;

	if (!readString(name, body, end) || !readString(password, body, end)) {
		sendPacket(PKT_GROUP_CREATE, error1, sizeof(error1)-1);
		pendingClose();
		return false;
	}

	printf("PKTGroupJoin %s, %s\n", name.c_str(), password.c_str());

	pname = dbescape(name.c_str());
	ppass = dbescape(password.c_str());

	res = dbexecf("SELECT id, lead FROM groups WHERE name=%s AND password=%s", pname, ppass);
	PQfreemem(pname);
	PQfreemem(ppass);

	if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) < 1) {
		sendPacket(PKT_GROUP_CREATE, error2, sizeof(error2)-1);
		pendingClose();
		PQclear(res);
		return false;
	}

	grId = atoi(PQgetvalue(res, 0, 0));
	leadId = atoi(PQgetvalue(res, 0, 1));
	res = dbexecf("UPDATE users SET goroups = (goroups || %d) WHERE id=%d AND (NOT ('{%d}' <@ goroups) OR goroups IS NULL)", grId, userId, grId);

	sendPacket(PKT_GROUP_JOIN, "\x00", 1);
	PKTGroupMemberUpdate(grId, name.c_str(), leadId, userId, MEMBER_MASK_CHANGE_ONLINE, nickName.c_str());
	PKTSingleGroupUpdate(grId);
	PQclear(res);
	return true;
}

/*обновление информации о группе*/
bool CPoolItemClient::PKTGroupInfo() {
	std::vector<int> groupIds;
	PGresult *res;
	char *str;
	int i;

	res = dbexecf("SELECT goroups FROM users WHERE id=%d", userId);
	str = PQgetvalue(res, 0, 0);
	if (!readArray(groupIds, str)) return true;
	PQclear(res);

	for (i = 0; i < groupIds.size(); i++) PKTSingleGroupUpdate(groupIds[i]);

	return true;
}

/*обновление информации о членах группы??*/
bool CPoolItemClient::PKTGroupMemberUpdate(int groupId, const char *grName, int leadId, int memberId, unsigned char flgs, const char *str) {
	char packetBuf[65536];
	PGresult *res;
	char *pname;
	int n, i;
	std::map<int, int> ids;
	std::vector<int> memberIds;

	int size = 7;
	*(int*)(packetBuf) = groupId;
	*(unsigned short*)(packetBuf+4) = 0;
	*(packetBuf+6) = strlen(grName)+1;

	memcpy(packetBuf+7, grName, strlen(grName)+1);
	size += strlen(grName)+1;

	*(int*)(packetBuf+size) = memberId;
	if (memberId == leadId) *(unsigned short*)(packetBuf+4+size) = flgs|MEMBER_MASK_CHANGE_MAKE_LEAD;
	else *(unsigned short*)(packetBuf+4+size) = flgs;
	*(packetBuf+6+size) = strlen(str)+1;

	memcpy(packetBuf+7+size, str, strlen(str)+1);
	size += strlen(str)+1 + 7;

	res = dbexecf("SELECT id FROM users WHERE '{%d}' && goroups", groupId);
	n = PQntuples(res);

	for (i=0; i < n; i++) ids[atoi(PQgetvalue(res, i, 0))] = i;
	PQclear(res);
	ids.erase(userId);

	sendToAll(ids, PKT_USER_GROUPINFO, packetBuf, size-1);
	return true;
}
 /*обновление информации о прайм группе*/
bool CPoolItemClient::PKTSingleGroupUpdate(int grId) {
	char packetBuf[65536];
	PGresult *res;
	int i, userCount, strSize, size, headSize, leadId, memberId;
	char *str;

	headSize = 7;
	size = headSize;
	*(int*)packetBuf = grId;
	if (primGr == grId) *(unsigned short*)(packetBuf+4) = GROUP_MASK_MAKE_PRIM;
	else *(unsigned short*)(packetBuf+4) = 0;

	res = dbexecf("SELECT name, lead FROM groups WHERE id=%d", grId);
	str = PQgetvalue(res, 0, 0);

	memcpy(packetBuf+size, str, strlen(str)+1);
	size += strlen(str)+1;
	*(packetBuf+6) = size-headSize;

	str = PQgetvalue(res, 0, 1);
	leadId = atoi(str);
	PQclear(res);

	res = dbexecf("SELECT id, name FROM users WHERE '{%d}' && goroups", grId);
	userCount = PQntuples(res);
	for (i = 0; i < userCount; i++) {
		str = PQgetvalue(res, i, 0);
		memberId = atoi(str);

		*(int*)(packetBuf+size) = memberId;
		if (memberId == leadId) *(unsigned short*)(packetBuf+size+4) = MEMBER_MASK_CHANGE_MAKE_LEAD;
		else *(unsigned short*)(packetBuf+size+4) = 0;

		str = PQgetvalue(res, i, 1);
		strSize = strlen(str)+1;
		memcpy(packetBuf+size+headSize, str, strSize);
		// strSize += strlen(usInf);

		*(packetBuf+size+6) = strSize;
		size += headSize + strSize;
	}

	PQclear(res);
	sendPacket(PKT_USER_GROUPINFO, packetBuf, size-1);
	return true;
}

/*проверка и отправка координаты метки всем в группе.*/
bool CPoolItemClient::PKTSendCoordsToAll(const char *body, int size) {
	PGresult *res;
	int count;
	char *str;
	std::map<int, int> ids;
	char error1[] = "\x01Неправильный пакет.";
	float longitude, latitude;
	char packetBuf[12];

	if (size < 8) {
		sendPacket(PKT_USER_COORDS, error1, sizeof(error1)-1);
		pendingClose();
		return false;
	}

	longitude = *(float*)body;
	latitude = *(float*)(body+4);

	printf("id=%d coords %f %f\n", userId, longitude, latitude);

	*(int*)packetBuf = userId;
	memcpy(packetBuf+4, body, 8);

	// res1 = dbexecf("SELECT goroups FROM users WHERE id=%d", userId);
	// str = PQgetvalue(res1, 0, 0);

	// res2 = dbexecf("SELECT id FROM users WHERE '%s' && goroups", str);
	// count = PQntuples(res2);
	// for (int i=0; i < count; i++) ids[atoi(PQgetvalue(res2, i, 0))] = i;
	// ids.erase(userId);
	// PQclear(res1);
	// PQclear(res2);

	res = dbexecf("SELECT id FROM users WHERE primgrid=%d", primGr);
	count = PQntuples(res);
	for (int i=0; i < count; i++) ids[atoi(PQgetvalue(res, i, 0))] = i;
	ids.erase(userId);
	PQclear(res);

	sendToAll(ids, PKT_USER_COORDS, packetBuf, sizeof(packetBuf));
	return true;
}

/*проверка и добавление метки на карту.*/
bool CPoolItemClient::PKTAddMarker(const char *body, int size) {
	char error1[] = "\x01Неправильный пакет.";
	const char *end = body + size;
	int type, color, markerId, count;
	float longitude, latitude;
	std::string description;
	std::map<int, int> ids;
	char *packetBuf = new char[size+4];
	PGresult *res, *res2;
	char *pdescription;

	type = *body++;
	color = *body++;

	if (!readString(description, body, end) || end - body < 8) {
		sendPacket(PKT_MARKER_CREATE, error1, sizeof(error1)-1);
		pendingClose();
		PQclear(res);
		return false;
	}

	latitude = *(float*)(body);
	longitude = *(float*)(body+4);

	pdescription = dbescape(description.c_str());

	res2 = dbexecf("SELECT id FROM markers WHERE type=0 AND (id = ANY((SELECT mapmarks FROM groups WHERE id=%d)::int[]))", primGr);
	count = PQntuples(res2);

	if (count && type == 0) {
		markerId = atoi(PQgetvalue(res2, 0, 0));
		res = dbexecf("UPDATE markers SET (type, color, latitude, longitude) = (%d, %d, %f, %f) WHERE id=%d RETURNING id", type, color, latitude, longitude, markerId);
	} else {
		res = dbexecf("INSERT INTO markers (type, color, description, latitude, longitude) VALUES (%d, %d, %s, %f, %f) RETURNING id", type, color, pdescription, latitude, longitude);
		PQfreemem(pdescription);

		if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) < 1) {
			sendPacket(PKT_MARKER_CREATE, error1, sizeof(error1)-1);
			pendingClose();
			PQclear(res);
			return false;
		}

		markerId = atoi(PQgetvalue(res, 0, 0));
	}
	PQclear(res);
	PQclear(res2);

	res = dbexecf("UPDATE groups SET mapmarks = (mapmarks || %d) WHERE id=%d AND (NOT ('{%d}' <@ mapmarks) OR mapmarks IS NULL)", markerId, primGr, markerId);
	PQclear(res);

	sendPacket(PKT_MARKER_CREATE, "\x00", 1);

	*(int*)packetBuf = markerId;
	memcpy(packetBuf+4, end-size, size);

	res = dbexecf("SELECT id FROM users WHERE '{%d}' && goroups", primGr);
	count = PQntuples(res);
	for (int i=0; i < count; i++) ids[atoi(PQgetvalue(res, i, 0))] = i;
	PQclear(res);

	sendToAll(ids, PKT_MARKER_ADD, packetBuf, size+4);
	delete[] packetBuf;
	return true;
}

/*???информация о метках???*/
bool CPoolItemClient::PKTMarkersInfo() {
	std::vector<int> markerIds;
	PGresult *res;
	char *description;
	char packetBuf[65536];
	int i, count;

	res = dbexecf("SELECT * FROM markers WHERE id = ANY((SELECT mapmarks FROM groups WHERE id=%d)::int[])", primGr);
	count = PQntuples(res);

	char *end = packetBuf;
	for (i = 0; i < count; i++) {
		*(int*)end = atoi(PQgetvalue(res, i, 0)); // id
		end += 4;

		*end++ = atoi(PQgetvalue(res, i, 1)); // type
		*end++ = atoi(PQgetvalue(res, i, 2)); // color

		description = PQgetvalue(res, i, 3); 
		memcpy(end, description, strlen(description)+1); // description
		end += strlen(description)+1;

		*(float*)end = atof(PQgetvalue(res, i, 4)); // latitude
		end += 4;
		*(float*)end = atof(PQgetvalue(res, i, 5)); // longitude
		end += 4;
	}

	PQclear(res);
	sendPacket(PKT_MARKER_ADD, packetBuf, end-packetBuf);
	return true;
}

// PKT_GROUP_CHANGE_LEAD
/*проверка ошибок и передача лидера.*/
bool CPoolItemClient::PKTGroupChangeLead(const char *body, int size) {
	char error1[] = "\x01Неправильный пакет.";
	char error2[] = "\x02Такой группы не существует.";
	char error3[] = "\x03Недостаточно прав!";
	char error4[] = "\x04Такого пользователя не существует или его нет в выбранной группе.";
	int newLeadId, grId;
	PGresult *res1, *res2, *res3;
	char *str, *grName, *leadName;

	if (size < 8) {
		sendPacket(PKT_GROUP_CHANGE_LEAD, error1, sizeof(error1)-1);
		pendingClose();
		return false;
	}

	grId = *(int*)body;
	newLeadId = *(int*)(body+4);

	res1 = dbexecf("SELECT lead, name FROM groups WHERE id=%d", grId);
	if (PQresultStatus(res1) != PGRES_TUPLES_OK || PQntuples(res1) < 1) {
		sendPacket(PKT_GROUP_CHANGE_LEAD, error2, sizeof(error2)-1);
		PQclear(res1);
		return false;
	}	

	if (userId != atoi(PQgetvalue(res1, 0, 0))) {
		sendPacket(PKT_GROUP_CHANGE_LEAD, error3, sizeof(error3)-1);
		PQclear(res1);
		return false;
	}
	grName = PQgetvalue(res1, 0, 1);

	res2 = dbexecf("SELECT name FROM users WHERE id=%d AND ('{%d}' <@ goroups)", newLeadId, grId);
	if (PQresultStatus(res2) != PGRES_TUPLES_OK || PQntuples(res2) < 1) {
		sendPacket(PKT_GROUP_CHANGE_LEAD, error4, sizeof(error4)-1);
		PQclear(res2);
		return false;
	}
	leadName = PQgetvalue(res2, 0, 0);

	res3 = dbexecf("UPDATE groups SET lead=%d WHERE id=%d", newLeadId, grId);

	PKTSingleGroupUpdate(grId);
	PKTGroupMemberUpdate(grId, grName, newLeadId, userId, MEMBER_MASK_CHANGE_ONLINE, nickName.c_str());
	PKTGroupMemberUpdate(grId, grName, newLeadId, newLeadId, MEMBER_MASK_CHANGE_ONLINE, leadName);
	sendPacket(PKT_GROUP_CHANGE_LEAD, "\x00", 1);

	PQclear(res1);
	PQclear(res2);
	PQclear(res3);
	return true;
}

/*проверка ошибок и изменение прайм группы.*/
bool CPoolItemClient::PKTChangePrimGr(const char *body, int size) {
	char error1[] = "\x01Неправильный пакет.";
	char error2[] = "\x02Такого пользователя не существует или он не состоит в данной группе.";
	PGresult *res;
	char packetBuf[5];
	int id;


	if (size < 4 || (id = *(int*)body) < 0) {
		sendPacket(PKT_USER_CHANGE_PRIMGR, error1, sizeof(error1)-1);
		pendingClose();
		return false;
	}

	res = dbexecf("SELECT id FROM users WHERE id=%d AND ('{%d}' <@ goroups)", userId, id);
	if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) < 1) {
		sendPacket(PKT_USER_CHANGE_PRIMGR, error2, sizeof(error2)-1);
		PQclear(res);
		return false;
	}
	PQclear(res);

	res = dbexecf("UPDATE users SET primgrid=%d WHERE id=%d AND ('{%d}' <@ goroups)", id, userId, id);
	PQclear(res);

	packetBuf[0] = 0;
	*(int*)(packetBuf+1) = id;

	sendPacket(PKT_USER_CHANGE_PRIMGR, packetBuf, sizeof(packetBuf));
	PKTSingleGroupUpdate(primGr);
	PKTSingleGroupUpdate(id);

	primGr = id;
	PKTMarkersInfo();
	return true;
}

// PKT_GROUP_EXIT
/*выход из группы.*/
bool CPoolItemClient::PTKExitGr(const char *body, int size) {
	char error1[] = "\x01Неправильный пакет.";
	PGresult *res, *res1;
	char *str;
	int id, leadId;

	if (size < 4 || (id = *(int*)body) < 0) {
		sendPacket(PKT_GROUP_EXIT, error1, sizeof(error1)-1);
		pendingClose();
		return false;
	}

	res = dbexecf("SELECT lead, name FROM groups WHERE id=%d", id);
	leadId = atoi(PQgetvalue(res, 0, 0));
	PKTGroupMemberUpdate(id, PQgetvalue(res, 0, 1), leadId, userId, MEMBER_MASK_CHANGE_NAME_OR_DELL, nickName.c_str());
	PQclear(res);

	res = dbexecf("UPDATE users SET goroups = array_remove(goroups, %d) WHERE id=%d", id, userId);
	PQclear(res);

	res = dbexecf("SELECT id, name FROM users WHERE ('{%d}' <@ goroups)", id);
	if (!PQntuples(res)) {
		PQclear(res);
		res = dbexecf("SELECT mapmarks FROM groups WHERE id=%d", id);
		res1  = dbexecf("DELETE FROM groups WHERE id=%d", id);
		PQclear(res1);

		str = PQgetvalue(res, 0, 0);
		if (strlen(str) > 2) {
			str[0] = '(';
			str[strlen(str)-1] = ')';
			printf("%s\n", str);
			res = dbexecf("DELETE FROM markers WHERE id IN %s", str);
		}

		PQclear(res);
	} else if (userId == leadId) {
		leadId = atoi(PQgetvalue(res, 0, 0));

		res1 = dbexecf("UPDATE groups SET lead=%d WHERE id=%d RETURNING name", leadId, id);
		PKTGroupMemberUpdate(id, PQgetvalue(res1, 0, 0), leadId, leadId, MEMBER_MASK_CHANGE_ONLINE, PQgetvalue(res, 0, 1));
		PQclear(res);
		PQclear(res1);
	}

	sendPacket(PKT_GROUP_EXIT, "\x00", 1);
	return true;
}

/*проверка ошибок и удаление из группы участника.*/
bool CPoolItemClient::PKTGroupKick(const char *body, int size) {
	char error1[] = "\x01Неправильный пакет.";
	char error2[] = "\x02Что-то пошло не так!";
	PGresult *res1, *res2;
	int memberId, grId;

	if (size < 8) {
		sendPacket(PKT_GROUP_KICK, error1, sizeof(error1)-1);
		pendingClose();
		return false;
	}

	grId = *(int*)body;
	memberId = *(int*)(body+4);

	res1 = dbexecf("SELECT lead FROM groups WHERE id=%d", grId);
	if (userId != atoi(PQgetvalue(res1, 0, 0))) {
		sendPacket(PKT_GROUP_KICK, error2, sizeof(error2)-1);
		PQclear(res1);
		return true;
	}
	PQclear(res1);

	res1 = dbexecf("SELECT name FROM users WHERE id=%d", memberId);
	res2 = dbexecf("SELECT name FROM groups WHERE id=%d", grId);
	PKTGroupMemberUpdate(grId, PQgetvalue(res2, 0, 0), userId, memberId, MEMBER_MASK_CHANGE_NAME_OR_DELL, PQgetvalue(res1, 0, 0));
	PQclear(res1);
	PQclear(res2);

	res1 = dbexecf("UPDATE users SET goroups = array_remove(goroups, %d) WHERE id=%d", grId, memberId);
	PQclear(res1);

	sendPacket(PKT_GROUP_KICK, "\x00", 1);	
	return true;
}

// PKT_MARKER_REM
/*проверка ошибок и удаление метки с карты.*/
bool CPoolItemClient::PKTDeleteMarker(const char *body, int size) {
	char error1[] = "\x01Неправильный пакет.";
	char error2[] = "\x02Неправильный тип данных.";
	std::map<int, int> ids;
	int markerId, grId, n, i;
	char packetBuf[5];
	PGresult *res;

	if (size < 8) {
		sendPacket(PKT_MARKER_REM, error1, sizeof(error1)-1);
		pendingClose();
		return true;
	}

	grId = *(int*)body;
	markerId = *(int*)(body+4);

	if (markerId < 0 || grId < 0) {
		sendPacket(PKT_MARKER_REM, error2, sizeof(error2)-1);
		return true;
	}

	res = dbexecf("DELETE FROM markers WHERE id=%d", markerId);
	PQclear(res);

	res = dbexecf("UPDATE groups SET mapmarks=array_remove(goroups, %d) WHERE id=%d", markerId, grId);
	PQclear(res);

	res = dbexecf("SELECT id FROM users WHERE primgrid=%d", grId);
	n = PQntuples(res);

	for (i=0; i < n; i++) ids[atoi(PQgetvalue(res, i, 0))] = i;
	PQclear(res);

	*packetBuf = 0;
	*(int*)(packetBuf+1) = markerId;

	sendToAll(ids, PKT_MARKER_REM, packetBuf, sizeof(packetBuf));
	return true;
}

// PQresultStatus(res) == PGRES_TUPLES_OK
// PQclear(res)
// PQnfields(res)
// PQntuples(res)
// PQerrorMessage(dbconn)
// i - PQntuples(res)
// j - PQnfields(res)
// PQgetvalue(res, i,j)

// printf("res = %p\n", res);
// printf("PQresultStatus = %d (%d)\n", PQresultStatus(res), PGRES_TUPLES_OK);
// printf("fields = %d tuples %d\n", PQnfields(res), PQntuples(res));
