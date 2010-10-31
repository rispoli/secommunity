#include <arpa/inet.h>
#include <boost/program_options.hpp>
namespace po = boost::program_options;
#include <errno.h>
#include <fstream>
#include <iostream>
#include "message.h"
#include <netinet/in.h>
#include <signal.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
using namespace std;

int termination_pipe[2];

void sig_handler(int) {
	FILE *p = fdopen(termination_pipe[1], "w");
	fprintf(p, "1");
	fclose(p);
	close(termination_pipe[1]);
}

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

int handle_query(string binary_path, string options, string kb_fn, int sock_fd, string log_fn, int log_level, struct in_addr sin_addr) {

	struct msg_c2s query;
	memset(query.query, 0, sizeof(query.query));
	if(recv(sock_fd, (void *)&query, sizeof(query), 0) == -1) {
		cerr << "Could not receive data (" << errno << ")" << endl;
		close(sock_fd);
		return 1;
	}

	stringstream pid_time; pid_time << getpid() << "_" << time(NULL);
	stringstream query_fn; query_fn << "/tmp/dlv-query_" << pid_time.str();
	string command = binary_path + options + " " + kb_fn + " " + query_fn.str() + " 2>&1";

	ofstream query_f(query_fn.str().c_str());
	if(!query_f.is_open()) {
		cerr << "Could not write temp. file: " << query_fn.str() << endl;
		close(sock_fd);
		return 1;
	}
	if(query.msg_type == SIMPLEQUERY)
		query_f << ":- " << query.query << ".";
	else if(query.msg_type == COUNTINGQUERY)
		query_f << "remote_count_" << pid_time.str() << "(X_" << pid_time.str() << ") :- #count{" << query.query << "} = X_" << pid_time.str() << ".";
	query_f.close();

	FILE *fpipe;
	char buffer_p[256];
	memset(buffer_p, 0, sizeof(buffer_p));

	if(!(fpipe = popen(command.c_str(), "r"))) {
		cerr << "Could not execute command: " << command << " (" << errno << ")" << endl;;
		close(sock_fd);
		delete_file(query_fn.str().c_str());
		return 1;
	}

	stringstream c_output;
	while(fgets(buffer_p, sizeof(buffer_p), fpipe))
		c_output << buffer_p;
	pclose(fpipe);

	struct msg_s2c answer;
	memset(answer.result, 0, sizeof(answer.result));

	if(c_output.str().find("parser errors") != string::npos) {
		strcpy(answer.result, "Aborted due to parser errors");
		answer.status = ERROR;
	} else if(query.msg_type == SIMPLEQUERY) {
		strcpy(answer.result, c_output.str() == "" ? "1" : "0");
		answer.status = SUCCESS;
	} else if(query.msg_type == COUNTINGQUERY) {
		stringstream str_to_search; str_to_search << "remote_count_" << pid_time.str();
		size_t p;
		if((p = c_output.str().find(str_to_search.str())) == string::npos) {
			strcpy(answer.result, "Could not count");
			answer.status = ERROR;
		} else {
			strcpy(answer.result, c_output.str().substr(p + str_to_search.str().length() + 1, c_output.str().find(")", p) - c_output.str().find("(", p) - 1).c_str());
			answer.status = SUCCESS;
		}
	}

	if(send(sock_fd, (void *)&answer, sizeof(answer), 0) == -1) {
		cerr << "Could not send answer (" << errno << ")" << endl;;
		close(sock_fd);
		delete_file(query_fn.str().c_str());
		return 1;
	}

	close(sock_fd);
	delete_file(query_fn.str().c_str());

	if(log_level > 0) {
		stringstream message;
		message << "received message from: " << inet_ntoa(sin_addr);
		if(log_level > 1)
			message << ", query: '" << query.query << "', result: " << answer.result;
		log_message(log_fn, message.str());
	}

	return 0;
}

int main(int argc, char *argv[]) {

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	string binary_path = "/usr/bin/dlv";
	string options = " -silent";
	string kb_fn = "kb.dlv";
	int port_no = 3333;
	string log_fn = "queries.log";
	int log_level = 1;

	po::options_description desc("Options");
	desc.add_options()
		("help,h", "display this help and exit")
		("daemonize,d", "daemonize this program")
		("dlv-path,e", po::value<string>(&binary_path)->default_value(binary_path), "dlv executable path")
		("knowledge-base,k", po::value<string>(&kb_fn)->default_value(kb_fn), "knowledge base path")
		("port-number,p", po::value<int>(&port_no)->default_value(port_no), "port number to listen on")
		("log-file,g", po::value<string>(&log_fn)->default_value(log_fn), "log-file path")
		("log-level,l", po::value<int>(&log_level)->default_value(log_level), "0 - off, 1 - default, 2 - verbose")
	;

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	if(vm.count("help")) {
		cout << desc << endl;
		return 1;
	}

	if(vm.count("daemonize")) {
		int pid = fork();
		if(pid < 0) {
			cerr << "Could not run in background" << endl;
			return 1;
		} else if(pid > 0) return 0;
		setsid();
	}

	if(pipe(termination_pipe) == -1) {
		cerr << "Could not create pipe (" << errno << ")" << endl;
		return 1;
	}

	int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(sock_fd == -1) {
		cerr << "Could not open socket (" << errno << ")" << endl;
		return 1;
	}
	int on = 1;
	int rc = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
	if(rc == -1) {
		cerr << "Could not set socket options (" << errno << ")" << endl;
		close(sock_fd);
		return 1;
	}

	struct sockaddr_in serv_addr, cli_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(port_no);
	if(bind(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
		cerr << "Could not bind port: " << port_no << endl;
		close(sock_fd);
		return 1;
	}
	listen(sock_fd, SOMAXCONN);
	stringstream startup; startup << "server started, listening on port: " << port_no;
	if(log_level > 0)
		log_message(log_fn, startup.str());
	signal(SIGCHLD, SIG_IGN);
	fd_set fd_set_s;
	int new_sock_fd, cli_len = sizeof(cli_addr);
	while(1) {
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

		if((new_sock_fd = accept(sock_fd, (struct sockaddr *)&cli_addr, (socklen_t *)&cli_len)) != -1) {
			int pid;
			switch(pid = fork()) {
				case -1:
					cerr << "Could not fork child (" << errno << ")" << endl;
					break;
				case 0:
					close(sock_fd);
					close(termination_pipe[0]);
					close(termination_pipe[1]);
					return handle_query(binary_path, options, kb_fn, new_sock_fd, log_fn, log_level, cli_addr.sin_addr);
				default:
					close(new_sock_fd);
			}
		} else {
			cerr << "Could not accept connection (" << errno << ")" << endl;
			close(sock_fd);
			return 1;
		}
	}

	if(log_level > 0)
		log_message(log_fn, "server stopped");
	return 0;
}
