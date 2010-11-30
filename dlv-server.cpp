#include <argtable2.h>
#include <arpa/inet.h>
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

int handle_query(string executable_path, string options, string kb_fn, int sock_fd, string log_fn, int log_level, struct in_addr sin_addr) {

	struct msg_c2s query;
	memset(query.query, 0, sizeof(query.query));
	if(recv(sock_fd, (void *)&query, sizeof(query), 0) == -1) {
		cerr << "Could not receive data (" << errno << ")" << endl;
		close(sock_fd);
		return 1;
	}

	stringstream pid_time; pid_time << getpid() << "_" << time(NULL);
	stringstream query_fn; query_fn << "/tmp/dlv-query_" << pid_time.str();
	string command = executable_path + options + " " + kb_fn + " " + query_fn.str() + " 2>&1";

	ofstream query_f(query_fn.str().c_str());
	if(!query_f.is_open()) {
		cerr << "Could not write temp. file: " << query_fn.str() << endl;
		close(sock_fd);
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
	} else if(query.msg_type == AGGREGATEQUERY) {
		stringstream str_to_search; str_to_search << "remote_" << query.aggregate_query << "_" << pid_time.str();
		size_t p;
		if((p = c_output.str().find(str_to_search.str())) == string::npos) {
			strcpy(answer.result, "Could not perform aggregate query");
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
			message << ", query: " << query.aggregate_query << (query.msg_type == AGGREGATEQUERY ? " " : "") << "'" << query.query << "', result: " << (query.msg_type == SIMPLEQUERY && answer.status == SUCCESS ? (c_output.str() == "" ? "true" : "false") : answer.result);
		log_message(log_fn, message.str());
	}

	return 0;
}

int main(int argc, char *argv[]) {

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	struct arg_lit *help = arg_lit0("h", "help", "print this help and exit");
	struct arg_lit *daemon = arg_lit0("d", "daemon", "run in background");
	struct arg_file *executable = arg_file0("e", "executable", NULL, "dlv executable path (default: /usr/bin/dlv)");
	struct arg_file *knowledge_base = arg_file0("k", "knowledge-base", NULL, "knowledge base path (default: kb.dlv)");
	struct arg_int *port_number = arg_int0("p", "port-number", NULL, "port number to listen on (default: 3333)");
	struct arg_file *log_file = arg_file0("L", "log-file", NULL, "log-file path (default: queries.log)");
	struct arg_int *log_l = arg_int0("l", "log-level", NULL, "0 - off, 1 - default, 2 - verbose");
	struct arg_end *end = arg_end(23);
	void *argtable[] = {help, daemon, executable, knowledge_base, port_number, log_file, log_l, end};

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

	if(daemon->count > 0) {
		int pid = fork();
		if(pid < 0) {
			cerr << "Could not run in background" << endl;
			return 1;
		} else if(pid > 0) return 0;
		setsid();
	}

	string executable_path = executable->filename[0];
	string options = " -silent";
	string kb_fn = knowledge_base->filename[0];
	int port_no = port_number->ival[0];
	string log_fn = log_file->filename[0];
	int log_level = log_l->ival[0];

	arg_freetable(argtable,sizeof(argtable)/sizeof(argtable[0]));

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
	stringstream startup; startup << "server started (pid: " << getpid() << "), listening on port: " << port_no << ", dlv: " << executable_path << ", knowledge base: " << kb_fn;
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
					return handle_query(executable_path, options, kb_fn, new_sock_fd, log_fn, log_level, cli_addr.sin_addr);
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
