/*

   Copyright 2010 Daniele Rispoli, Valerio Genovese, Steve Barker

   This file is part of dlv-server.

   dlv-server is free software: you can redistribute it and/or modify
   it under the terms of the GNU Affero General Public License as
   published by the Free Software Foundation, either version 3 of the
   License, or (at your option) any later version.

   dlv-server is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public
   License along with dlv-server.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <argtable2.h>
#include <errno.h>
#include <fstream>
#include <iostream>
#include "message.h"
#include <signal.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
using namespace std;

#if defined (WIN32) && !defined (__CYGWIN32__) // It's not Unix, really. See? Capital letters.

#define __WINDOWS

#define _WIN32_WINNT 0x501 // To get getaddrinfo && freeaddrinfo working with MinGW.
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define DIR_TEMP_FILES ".\\"
#define SCAST const char
#define RCAST char
#define getpid() GetCurrentProcessId()
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val))

string executable_path;
string options;
string kb_fn;
string log_fn;
int log_level;
struct in_addr sin_addr;

#else

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define DIR_TEMP_FILES "/tmp/"
#define SCAST void
#define RCAST void
#define closesocket(fd) close(fd)

#endif

#ifdef __WINDOWS
void log_message(string filename, string message);
BOOL CtrlHandler(DWORD) {
	if(log_level > 0)
		log_message(log_fn, "server stopped");

	WSACleanup();

	return 0;
}
#else
int termination_pipe[2];

void sig_handler(int) {
	FILE *p = fdopen(termination_pipe[1], "w");
	fprintf(p, "1");
	fclose(p);
	close(termination_pipe[1]);
}
#endif

void log_message(string filename, string message) {
	FILE *logfile = fopen(filename.c_str(), "a");
	if(!logfile) {
		cerr << "Could not write to log file: " << filename << endl;
		return;
	}
	stringstream ss_time;
	time_t now = time(NULL);
	ss_time << ctime(&now);
	fprintf(logfile, "%s\n", ("[" + ss_time.str().substr(0, ss_time.str().length() -1) + "] " + message).c_str());
	fclose(logfile);
}

void delete_file(const char *filename) {
	if(remove(filename) != 0)
		cerr << "Could not delete temp. file: " << *filename << " (" << errno << ")" << endl;
}

#ifdef __WINDOWS
DWORD WINAPI handle_query(void *lp) {
	int &sock_fd = *(int *)lp;
#else
int handle_query(string executable_path, string options, string kb_fn, int sock_fd, string log_fn, int log_level, struct in_addr sin_addr) {
	signal(SIGCHLD, SIG_DFL);
#endif

	struct msg_c2s query;
	memset(query.query, 0, sizeof(query.query));
	if(recv(sock_fd, (RCAST *)&query, sizeof(query), 0) == -1) {
		cerr << "Could not receive data (" << errno << ")" << endl;
		closesocket(sock_fd);
		return 1;
	}

	if(!strcmp(query.query, "")) {
		closesocket(sock_fd);
		return 1;
	}

	stringstream pid_time; pid_time << getpid() << "_" << time(NULL);
	stringstream query_fn; query_fn << DIR_TEMP_FILES << "dlv-query_" << pid_time.str();
	stringstream iplist_fn; iplist_fn << DIR_TEMP_FILES << "dlv-iplist_" << pid_time.str();
	ofstream iplist_f(iplist_fn.str().c_str());
	if(!iplist_f.is_open()) {
		cerr << "Could not write temp. file: " << iplist_fn.str() << endl;
		closesocket(sock_fd);
		return 1;
	}
	for(int i = 0; i < query.a_counter; i++)
		iplist_f << query.addresses[i].ip << ":" << query.addresses[i].port << endl;
	iplist_f.close();

	ifstream kb_f_b(kb_fn.c_str());
	stringstream kb_fn_a; kb_fn_a << DIR_TEMP_FILES << "dlv-kb_" << pid_time.str();
	ofstream kb_f_a(kb_fn_a.str().c_str());
	if(!kb_f_b.is_open() || !kb_f_a.is_open()) {
		cerr << "Could not open knowledge base" << endl;
		closesocket(sock_fd);
		return 1;
	}

	string line;
	while(kb_f_b.good()) {
		getline(kb_f_b, line);
		unsigned int p_h = 0;
		while((p_h = line.find("#", p_h)) != string::npos) {
			unsigned int p_o;
			if((p_o = line.find("(", p_h)) != string::npos && (!line.compare(p_h + 1, p_o - p_h - 1, "at") || !line.compare(p_h + 1, p_o - p_h - 1, "count_at") || !line.compare(p_h + 1, p_o - p_h - 1, "sum_at") || !line.compare(p_h + 1, p_o - p_h - 1, "times_at") || !line.compare(p_h + 1, p_o - p_h - 1, "min_at") || !line.compare(p_h + 1, p_o - p_h - 1, "max_at"))) {
				unsigned int next_or_end = line.find("#", p_o);
				unsigned int p_c = line.rfind(")", next_or_end);
				line.insert(p_c, ", \"" + iplist_fn.str() + "\"");
				p_h = p_c + iplist_fn.str().length();
			} else p_h = string::npos;
		}
		kb_f_a << line << endl;
	}

	kb_f_b.close();
	kb_f_a.close();

	string command = executable_path + options + " " + kb_fn_a.str() + " " + query_fn.str() + " 2>&1";

	ofstream query_f(query_fn.str().c_str());
	if(!query_f.is_open()) {
		cerr << "Could not write temp. file: " << query_fn.str() << endl;
		closesocket(sock_fd);
		return 1;
	}
	if(query.msg_type == SIMPLEQUERY)
		query_f << ":- " << query.query << ".";
	else if(query.msg_type == AGGREGATEQUERY)
		query_f << "remote_" << query.aggregate_query << "_" << pid_time.str() << "(X_" << pid_time.str() << ") :- #" << query.aggregate_query << "{" << query.query << "} = X_" << pid_time.str() << ".";
	query_f.close();

	FILE *fpipe;
	char buffer_p[256];
	memset(buffer_p, 0, sizeof(buffer_p));

	if(!(fpipe = popen(command.c_str(), "r"))) {
		cerr << "Could not execute command: " << command << " (" << errno << ")" << endl;;
		closesocket(sock_fd);
		delete_file(query_fn.str().c_str());
		delete_file(iplist_fn.str().c_str());
		delete_file(kb_fn_a.str().c_str());
		return 1;
	}

	stringstream c_output;
	while(fgets(buffer_p, sizeof(buffer_p), fpipe))
		c_output << buffer_p;
	int exit_status = pclose(fpipe);

	struct msg_s2c answer;
	memset(answer.result, 0, sizeof(answer.result));

	if(WEXITSTATUS(exit_status)) {
		strncpy(answer.result, c_output.str().c_str(), sizeof(answer.result));
		answer.status = DLV_ERROR;
	} else if(c_output.str().find("Loop detected: aborting") != string::npos) {
		strncpy(answer.result, "Loop detected: aborting", sizeof(answer.result));
		answer.status = DLV_ERROR;
	} else {
		if(query.msg_type == SIMPLEQUERY) {
			strcpy(answer.result, c_output.str() == "" ? "1" : "0");
			answer.status = DLV_SUCCESS;
		} else if(query.msg_type == AGGREGATEQUERY) {
			stringstream str_to_search; str_to_search << "remote_" << query.aggregate_query << "_" << pid_time.str();
			size_t p = c_output.str().find(str_to_search.str());
			strcpy(answer.result, c_output.str().substr(p + str_to_search.str().length() + 1, c_output.str().find(")", p) - c_output.str().find("(", p) - 1).c_str());
			answer.status = DLV_SUCCESS;
		}
	}

	if(send(sock_fd, (SCAST *)&answer, sizeof(answer), 0) == -1) {
		cerr << "Could not send answer (" << errno << ")" << endl;;
		closesocket(sock_fd);
		delete_file(query_fn.str().c_str());
		delete_file(iplist_fn.str().c_str());
		delete_file(kb_fn_a.str().c_str());
		return 1;
	}

	closesocket(sock_fd);
	delete_file(query_fn.str().c_str());
	delete_file(iplist_fn.str().c_str());
	delete_file(kb_fn_a.str().c_str());

	if(log_level > 0) {
		stringstream message;
		message << "received message from: " << inet_ntoa(sin_addr);
		if(log_level > 1)
			message << ", query: " << query.aggregate_query << (query.msg_type == AGGREGATEQUERY ? " " : "") << "'" << query.query << "', result: " << (query.msg_type == SIMPLEQUERY && answer.status == DLV_SUCCESS ? (c_output.str() == "" ? "true" : "false") : answer.result);
		log_message(log_fn, message.str());
	}

	return 0;
}

void closesocket_and_cleanup(int sock_fd) {
	closesocket(sock_fd);
#ifdef __WINDOWS
	WSACleanup();
#endif
}

int main(int argc, char *argv[]) {

#ifdef __WINDOWS
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
#else
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
#endif

	struct arg_lit *help = arg_lit0("h", "help", "print this help and exit");
#ifndef __WINDOWS
	struct arg_lit *daemon = arg_lit0("d", "daemon", "run in background");
#endif
	struct arg_file *executable = arg_file0("e", "executable", NULL, "dlv executable path (default: /usr/bin/dlv)");
	struct arg_file *knowledge_base = arg_file0("k", "knowledge-base", NULL, "knowledge base path (default: kb.dlv)");
	struct arg_int *port_number = arg_int0("p", "port-number", NULL, "port number to listen on (default: 3333)");
	struct arg_file *log_file = arg_file0("L", "log-file", NULL, "log-file path (default: queries.log)");
	struct arg_int *log_l = arg_int0("l", "log-level", NULL, "0 - off, 1 - default, 2 - verbose");
	struct arg_end *end = arg_end(23);
#ifdef __WINDOWS
	void *argtable[] = {help, executable, knowledge_base, port_number, log_file, log_l, end};
#else
	void *argtable[] = {help, daemon, executable, knowledge_base, port_number, log_file, log_l, end};
#endif

	if(arg_nullcheck(argtable) != 0) {
		cerr << "Could not allocate enough memory for command line arguments" << endl;
		arg_freetable(argtable,sizeof(argtable)/sizeof(argtable[0]));
		return 1;
	}

	executable->filename[0] = "/usr/bin/dlv";
	knowledge_base->filename[0] = "kb.dlv";
	port_number->ival[0] = 3333;
	log_file->filename[0] =  "queries.log";
	log_l->ival[0] = 1;

	int nerrors = arg_parse(argc, argv, argtable);

	if(help->count > 0) {
		cout << "Usage: " << argv[0];
		arg_print_syntaxv(stdout, argtable, "\n");
		arg_print_glossary(stdout, argtable, "  %-30s %s\n");
		arg_freetable(argtable,sizeof(argtable)/sizeof(argtable[0]));
		return 0;
	}

	if(nerrors > 0) {
		arg_print_errors(stdout, end, argv[0]);
		arg_freetable(argtable,sizeof(argtable)/sizeof(argtable[0]));
		return 1;
	}

#ifndef __WINDOWS
	if(daemon->count > 0) {
		int pid = fork();
		if(pid < 0) {
			cerr << "Could not run in background" << endl;
			return 1;
		} else if(pid > 0) return 0;
		setsid();
	}
#endif

#ifdef __WINDOWS
	executable_path = executable->filename[0];
	options = " -silent";
	kb_fn = knowledge_base->filename[0];
	int port_no = port_number->ival[0];
	log_fn = log_file->filename[0];
	log_level = log_l->ival[0];
#else
	string executable_path = executable->filename[0];
	string options = " -silent";
	string kb_fn = knowledge_base->filename[0];
	int port_no = port_number->ival[0];
	string log_fn = log_file->filename[0];
	int log_level = log_l->ival[0];
#endif

	arg_freetable(argtable,sizeof(argtable)/sizeof(argtable[0]));

#ifndef __WINDOWS
	if(pipe(termination_pipe) == -1) {
		cerr << "Could not create pipe (" << errno << ")" << endl;
		return 1;
	}
#endif

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

	int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(sock_fd == -1) {
		cerr << "Could not open socket (" << errno << ")" << endl;
		return 1;
	}
	int on = 1;
	int rc = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
	if(rc == -1) {
		cerr << "Could not set socket options (" << errno << ")" << endl;
		closesocket_and_cleanup(sock_fd);
		return 1;
	}

	struct sockaddr_in serv_addr, cli_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(port_no);
	if(bind(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
		cerr << "Could not bind port: " << port_no << endl;
		closesocket_and_cleanup(sock_fd);
		return 1;
	}
	listen(sock_fd, SOMAXCONN);
	stringstream startup; startup << "server started (pid: " << getpid() << "), listening on port: " << port_no << ", dlv: " << executable_path << ", knowledge base: " << kb_fn;
	if(log_level > 0)
		log_message(log_fn, startup.str());
#ifndef __WINDOWS
	signal(SIGCHLD, SIG_IGN);
	fd_set fd_set_s;
#endif
	int new_sock_fd, cli_len = sizeof(cli_addr);
	while(1) {
#ifndef __WINDOWS
		do {
			FD_ZERO(&fd_set_s);
			FD_SET(sock_fd, &fd_set_s);
			FD_SET(termination_pipe[0], &fd_set_s);
			rc = select((sock_fd >= termination_pipe[0] ? sock_fd : termination_pipe[0]) + 1, &fd_set_s, NULL, NULL, NULL);
		} while((rc == -1) && (errno == EINTR));

		if(rc == -1) {
			cerr << "Could not select (" << errno << ")" << endl;
			perror("-->");
			close(sock_fd);
			close(termination_pipe[0]);
			close(termination_pipe[1]);
			return 1;
		}

		if(FD_ISSET(termination_pipe[0], &fd_set_s)) {
			close(sock_fd);
			FD_CLR(sock_fd, &fd_set_s);
			close(termination_pipe[0]);
			FD_CLR(termination_pipe[0], &fd_set_s);
			break;
		}
#endif

		if((new_sock_fd = accept(sock_fd, (struct sockaddr *)&cli_addr, (socklen_t *)&cli_len)) != -1) {
#ifdef __WINDOWS
			sin_addr = cli_addr.sin_addr;
			CreateThread(0, 0, &handle_query, (void *)&new_sock_fd , 0, 0);
#else
			int pid;
			switch(pid = fork()) {
				case -1:
					cerr << "Could not fork child (" << errno << ")" << endl;
					break;
				case 0:
					close(sock_fd);
					close(termination_pipe[0]);
					close(termination_pipe[1]);
					return handle_query(executable_path, options, kb_fn, new_sock_fd, log_fn, log_level, cli_addr.sin_addr);
				default:
					close(new_sock_fd);
			}
#endif
		} else {
			cerr << "Could not accept connection (" << errno << ")" << endl;
			closesocket_and_cleanup(sock_fd);
			return 1;
		}
	}

	closesocket_and_cleanup(sock_fd);

	if(log_level > 0)
		log_message(log_fn, "server stopped");
	return 0;
}
