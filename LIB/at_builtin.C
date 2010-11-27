#include <errno.h>
#include "extpred.h"
#include <iostream>
#include "../message.h"
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#define AGGREGATE(function) \
	BUILTIN(function##_at, iii) { \
		return false; \
	} \
	BUILTIN(function##_at, iio) { \
		if(argv[0].isString() && argv[1].isString()) { \
			string iporhostname(argv[0].toString()); \
			string query(argv[1].toString()); \
			int r = process_query(iporhostname, query, AGGREGATEQUERY, #function); \
			argv[2] = CONSTANT(r); \
			return r != ERROR; \
		} \
		return false; \
	}

int process_query(string iporhostname, string query, int msg_type, string aggregate_query) {
	int sock_fd = -1, rv;
	string port("3333");

	if((rv = iporhostname.find(":")) != string::npos) {
		port = iporhostname.substr(rv + 1, iporhostname.length() - rv);
		iporhostname = iporhostname.substr(0, rv);
	}

	struct addrinfo hints, *serv_info, *p;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if(getaddrinfo(iporhostname.c_str(), port.c_str(), &hints, &serv_info) != 0) {
		cerr << "Could not resolve host (" << errno << ")" << endl;
		return ERROR;
	}

	for(p = serv_info; p != NULL; p = p->ai_next) {
		if((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			cerr << "Could not open socket (" << errno << ")" << endl;
			continue;
		}

		if(connect(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
			cerr << "Could not connect (" << errno << ")" << endl;
			close(sock_fd);
			continue;
		}

		break;
	}

	if(p == NULL) {
		cerr << "Failed to connect" << endl;
		close(sock_fd);
		freeaddrinfo(serv_info);
		return ERROR;
	}

	freeaddrinfo(serv_info);

	struct msg_c2s msg;
	memset(msg.query, 0, sizeof(msg.query));
	strncpy(msg.query, query.c_str(), sizeof(msg.query));
	strncpy(msg.aggregate_query, aggregate_query.c_str(), sizeof(msg.aggregate_query));
	msg.msg_type = msg_type;
	if(send(sock_fd, (void *)&msg, sizeof(msg), 0) == -1) {
		cerr << "Could not send query (" << errno << ")" << endl;;
		close(sock_fd);
		return ERROR;
	}

	struct msg_s2c answer;
	memset(answer.result, 0, sizeof(answer.result));
	if(recv(sock_fd, (void *)&answer, sizeof(answer), 0) == -1) {
		cerr << "Could not receive result (" << errno << ")" << endl;
		close(sock_fd);
		return ERROR;
	}

	close(sock_fd);

	if(answer.status == ERROR) {
		cerr << iporhostname << ":" << port << " says: " << answer.result << endl;
		return ERROR;
	}

	return atoi(answer.result);
}

#ifdef __cplusplus
extern "C" {
#endif

	BUILTIN(at, ii) {
		if(argv[0].isString() && argv[1].isSymbol()) {

			string iporhostname(argv[0].toString());
			string query(argv[1].toSymbol());

			return process_query(iporhostname, query, SIMPLEQUERY, "") == SUCCESS;

		}

		return false;
	}

	AGGREGATE(count)
	AGGREGATE(sum)
	AGGREGATE(times)
	AGGREGATE(min)
	AGGREGATE(max)

#ifdef __cplusplus
}
#endif
