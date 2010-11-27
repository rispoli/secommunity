#define SIMPLEQUERY 0
#define AGGREGATEQUERY 1

struct msg_c2s {
	char query[1024];
	char aggregate_query[5]; // count, sum, times, min, max
	int msg_type;
};

#define SUCCESS 1
#define ERROR -1

struct msg_s2c {
	char result[128];
	int status;
};
