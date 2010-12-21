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
#include <iostream>
#include "../message.h"
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/types.h>

#if defined (WIN32) && !defined (__CYGWIN32__) // It's not Unix, really. See? Capital letters.

#define __WINDOWS

#pragma comment(lib, "Ws2_32.lib") // link with Ws2_32.lib

#define _WIN32_WINNT 0x501 // To get getaddrinfo && freeaddrinfo working with MinGW.
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define close(fd) closesocket(fd)
#define SCAST const char
#define RCAST char

#else

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
	BUILTIN(function##_at, iio) { \
		if(argv[0].isString() && argv[1].isString()) { \
			string iporhostname(argv[0].toString()); \
			string query(argv[1].toString()); \
			int r = process_query(iporhostname, query, AGGREGATEQUERY, #function); \
			argv[2] = CONSTANT(r); \
			return r != DLV_ERROR; \
		} \
		return false; \
	}

void close_and_cleanup(int sock_fd) {
	close(sock_fd);
#ifdef __WINDOWS
	WSACleanup();
#endif
}

int process_query(string iporhostname, string query, int msg_type, string aggregate_query) {
	int sock_fd = -1, rv;
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

	freeaddrinfo(serv_info);

	struct msg_c2s msg;
	memset(msg.query, 0, sizeof(msg.query));
	if(query.size() > sizeof(msg.query)) {
		cerr << "Network buffer size exceeded: split '" << query << "'" << endl;
		close_and_cleanup(sock_fd);
		return DLV_ERROR;
	}
	strncpy(msg.query, query.c_str(), sizeof(msg.query));
	strncpy(msg.aggregate_query, aggregate_query.c_str(), sizeof(msg.aggregate_query));
	msg.msg_type = msg_type;
	if(send(sock_fd, (SCAST *)&msg, sizeof(msg), 0) == -1) {
		cerr << "Could not send query (" << errno << ")" << endl;;
		close_and_cleanup(sock_fd);
		return DLV_ERROR;
	}

	struct msg_s2c answer;
	memset(answer.result, 0, sizeof(answer.result));
	if(recv(sock_fd, (RCAST *)&answer, sizeof(answer), 0) == -1) {
		cerr << "Could not receive result (" << errno << ")" << endl;
		close_and_cleanup(sock_fd);
		return DLV_ERROR;
	}

	close_and_cleanup(sock_fd);

	if(answer.status == DLV_ERROR) {
		cerr << iporhostname << ":" << port << " says: " << answer.result << endl;
		return DLV_ERROR;
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

			return process_query(iporhostname, query, SIMPLEQUERY, "") == DLV_SUCCESS;

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
