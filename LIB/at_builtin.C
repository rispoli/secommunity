#include <errno.h>
#include "extpred.h"
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

	BUILTIN(at, ii) {
		if(argv[0].isString() && argv[1].isSymbol()) {

			int sock_fd = -1, rv;

			string query(argv[1].toSymbol());
			string iporhostname(argv[0].toString()), port("3333");

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
				return false;
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
				return false;
			}

			freeaddrinfo(serv_info);

			if(send(sock_fd, query.c_str(), strlen(query.c_str()), 0) == -1) {
				cerr << "Could not send query (" << errno << ")" << endl;;
				close(sock_fd);
				return false;
			}

			char buffer_s[1];
			memset(buffer_s, 0, sizeof(buffer_s));
			if(recv(sock_fd, buffer_s, sizeof(buffer_s), 0) == -1) {
				cerr << "Could not receive result (" << errno << ")" << endl;
				close(sock_fd);
				return false;
			}

			close(sock_fd);

			return atoi(buffer_s);
		}

		return false;
	}

#ifdef __cplusplus
}
#endif
