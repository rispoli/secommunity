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

#define INET6_ADDRSTRLEN 46

struct address {
	char ip[INET6_ADDRSTRLEN];
	int port;
};

#define SIMPLEQUERY 0
#define AGGREGATEQUERY 1

#define MAX_DEPTH 10

struct msg_c2s {
	char query[256];
	char aggregate_query[7]; // count, sum, times, min, max
	int msg_type;
	struct address addresses[MAX_DEPTH];
	int a_counter;
};

#define DLV_SUCCESS 1
#define DLV_ERROR -1

struct msg_s2c {
	char result[256];
	int status;
};
