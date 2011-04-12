/*

   Copyright 2010 Daniele Rispoli, Valerio Genovese, Steve Barker

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

   Linking at_builtin statically or dynamically with other modules is making a
   combined work based on at_builtin. Thus, the terms and conditions of the GNU
   General Public License cover the whole combination.

   In addition, as a special exception, the copyright holders of at_builtin give
   you permission to combine at_builtin program with free software programs or
   libraries that are released under the GNU LGPL and with independent modules
   that communicate with at_builtin solely through the interface provided by
   extpred.h as developed and distributed in the context of the DLV-EX Project 
   (https://www.mat.unical.it/ianni/wiki/dlvex). You may copy and distribute
   such a system following the terms of the GNU GPL for at_builtin and the
   licenses of the other code concerned, provided that you include the source
   code of that other code when and as the GNU GPL requires distribution of
   source code.

   Note that people who make modified versions of at_builtin are not obligated
   to grant this special exception for their modified versions; it is their
   choice whether to do so. The GNU General Public License gives permission to
   release a modified version without this exception; this exception also makes
   it possible to release a modified version which carries forward this
   exception.

*/

#include <errno.h>
#include "extpred.h"
#include <fstream>
#include <iostream>
#include "../message.h"
#include <sstream>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/types.h>

#if defined (WIN32) && !defined (__CYGWIN32__) // It's not Unix, really. See? Capital letters.

#define __WINDOWS

#define _WIN32_WINNT 0x501 // To get getaddrinfo && freeaddrinfo working with MinGW.
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define close(fd) closesocket(fd)
#define SCAST const char
#define RCAST char

#else

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define SCAST void
#define RCAST void

#endif

#define AGGREGATE(function) \
	BUILTIN(function##_at, iii) { \
		return false; \
	} \
	BUILTIN(function##_at, iioi) { \
		if(argv[0].isString() && argv[1].isString()) { \
			string iporhostname(argv[0].toString()); \
			string query(argv[1].toString()); \
			string iplist_fn((argc == 4 && argv[3].isString()) ? argv[3].toString() : ""); \
			int r = process_query(iporhostname, query, iplist_fn, AGGREGATEQUERY, #function); \
			argv[2] = CONSTANT(r); \
			return r != DLV_ERROR; \
		} \
		return false; \
	} \
	BUILTIN(function##_at, iio) { \
		return function##_at_iioi(argv, argc); \
	}

void close_and_cleanup(int sock_fd) {
	close(sock_fd);
#ifdef __WINDOWS
	WSACleanup();
#endif
}

#ifdef __WINDOWS
#include "ntop.h"
#endif

int process_query(string iporhostname, string query, string iplist_fn, int msg_type, string aggregate_query) {
	int sock_fd = -1;
	unsigned int rv;
	string port("3333");

	if((rv = iporhostname.find(":")) != string::npos) {
		port = iporhostname.substr(rv + 1, iporhostname.length() - rv);
		iporhostname = iporhostname.substr(0, rv);
	}

#ifdef __WINDOWS
	WSADATA wsaData;

	if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		cerr << "Could not find Winsock dll" << endl;
		return DLV_ERROR;
	}

	if(LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
		cerr << "Wrong Winsock version" << endl;
		WSACleanup();
		return DLV_ERROR;
	}
#endif

	struct addrinfo hints, *serv_info, *p;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if(getaddrinfo(iporhostname.c_str(), port.c_str(), &hints, &serv_info) != 0) {
		cerr << "Could not resolve host (" << errno << ")" << endl;
#ifdef __WINDOWS
		WSACleanup();
#endif
		return DLV_ERROR;
	}

	for(p = serv_info; p != NULL; p = p->ai_next) {
		if((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			cerr << "Could not open socket (" << errno << ")" << endl;
#ifdef __WINDOWS
			WSACleanup();
#endif
			continue;
		}

		if(connect(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
			cerr << "Could not connect (" << errno << ")" << endl;
			close_and_cleanup(sock_fd);
			continue;
		}

		break;
	}

	if(p == NULL) {
		cerr << "Failed to connect" << endl;
		close_and_cleanup(sock_fd);
		freeaddrinfo(serv_info);
		return DLV_ERROR;
	}

	char query_ip[INET6_ADDRSTRLEN];
	struct sockaddr_in const *sin;
	sin = (struct sockaddr_in *)serv_info->ai_addr;

	if(inet_ntop(serv_info->ai_family, &sin->sin_addr, query_ip, sizeof(query_ip)) == NULL) {
		cerr << "Could not get remote server's info" << endl;
		close_and_cleanup(sock_fd);
		freeaddrinfo(serv_info);
		return DLV_ERROR;
	}

	freeaddrinfo(serv_info);

	struct msg_c2s msg;
	msg.query_size = query.size();
	strncpy(msg.aggregate_query, aggregate_query.c_str(), sizeof(msg.aggregate_query));
	msg.msg_type = msg_type;
	vector< pair<string, int> > v;
	if(iplist_fn != "") {
		ifstream ipfile(iplist_fn.c_str());
		if(!ipfile.is_open()) {
			cerr << "Could not open IP list" << endl;
			close_and_cleanup(sock_fd);
			return DLV_ERROR;
		}
		string line;
		while(ipfile.good()) {
			getline(ipfile, line);
			unsigned int rv;
			if((rv = line.find(":")) != string::npos) {
				pair<string, int> p(line.substr(0, rv), atoi(line.substr(rv + 1, line.length() - rv).c_str()));
				v.push_back(p);
				if(atoi(port.c_str()) == p.second && !strcmp(query_ip, p.first.c_str())) {
					cerr << "Loop detected: aborting" << endl;
					close_and_cleanup(sock_fd);
					return DLV_ERROR;
				}
			}
		}
		ipfile.close();
	}
	v.push_back(make_pair(query_ip, atoi(port.c_str())));
	msg.a_counter = v.size();

	if(send(sock_fd, (SCAST *)&msg, sizeof(msg), 0) == -1 || send(sock_fd, (SCAST *)query.c_str(), query.size(), 0) == -1) {
		cerr << "Could not send query (" << errno << ")" << endl;
		close_and_cleanup(sock_fd);
		return DLV_ERROR;
	}

	for(vector< pair<string, int> >::iterator it = v.begin(); it < v.end(); it++) {
		struct address a;
		a.port = it->second;
		memset(a.ip, 0, sizeof(a.ip));
		strncpy(a.ip, it->first.c_str(), sizeof(a.ip));
		if(send(sock_fd, (SCAST *)&a, sizeof(a), 0) == -1) {
			cerr << "Could not send query (" << errno << ")" << endl;
			close_and_cleanup(sock_fd);
			return DLV_ERROR;
		}
	}

	struct msg_s2c answer;
	if(recv(sock_fd, (RCAST *)&answer, sizeof(answer), 0) == -1) {
		cerr << "Could not receive result (" << errno << ")" << endl;
		close_and_cleanup(sock_fd);
		return DLV_ERROR;
	}

	char *answer_result = (char *)malloc(answer.result_size + 1);
	memset(answer_result, 0, answer.result_size + 1);
	if(recv(sock_fd, (RCAST *)answer_result, answer.result_size, 0) == -1) {
		cerr << "Could not receive data (" << errno << ")" << endl;
		close_and_cleanup(sock_fd);
		free(answer_result);
		return 1;
	}

	close_and_cleanup(sock_fd);

	if(answer.status == DLV_ERROR) {
		cerr << iporhostname << ":" << port << " says: " << answer_result << endl;
		free(answer_result);
		return DLV_ERROR;
	}

	int r = atoi(answer_result);
	free(answer_result);

	return r;
}

#ifdef __cplusplus
extern "C" {
#endif

	BUILTIN(at, iii) {
		if(argv[0].isString() && argv[1].isSymbol()) {

			string iporhostname(argv[0].toString());
			string query(argv[1].toSymbol());
			string iplist_fn((argc == 3 && argv[2].isString()) ? argv[2].toString() : "");

			return process_query(iporhostname, query, iplist_fn, SIMPLEQUERY, "") == DLV_SUCCESS;

		}

		return false;
	}

	BUILTIN(at, ii) {
		return at_iii(argv, argc);
	}

	AGGREGATE(count)
	AGGREGATE(sum)
	AGGREGATE(times)
	AGGREGATE(min)
	AGGREGATE(max)

#ifdef __cplusplus
}
#endif
