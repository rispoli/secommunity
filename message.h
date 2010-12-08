#define SIMPLEQUERY 0
#define AGGREGATEQUERY 1

struct msg_c2s {
	char query[1024];
	char aggregate_query[7]; // count, sum, times, min, max
	int msg_type;
};

#define DLV_SUCCESS 1
#define DLV_ERROR -1

struct msg_s2c {
	char result[256];
	int status;
};
