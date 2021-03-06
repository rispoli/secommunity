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

struct history_item {
	int q_identifier;
	char ip[INET6_ADDRSTRLEN];
	int port;
};

#define SIMPLEQUERY 0
#define AGGREGATEQUERY 1

struct msg_c2s {
	size_t query_size;
	char aggregate_query[7]; // count, sum, times, min, max
	int msg_type;
	size_t h_counter;
};

#define DLV_SUCCESS 1
#define DLV_ERROR -1

struct msg_s2c {
	size_t result_size;
	int status;
};
