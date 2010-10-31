#define SIMPLEQUERY 0
#define COUNTINGQUERY 1

struct msg_c2s {
	char query[1024];
	int msg_type;
};

#define SUCCESS 1
#define ERROR -1

struct msg_s2c {
	char result[128];
	int status;
};
