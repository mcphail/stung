/*
 *    stung - Server for Transmitting Updated NICE Guidelines
 *
 *    Copyright (C) 2011 Neil McPhail
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <dirent.h>

#define UDP_CHALL_SIZE 3
#define UDP_RESP_SIZE 32
#define TCP_READBUF_SIZE 37

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ll_mutex = PTHREAD_MUTEX_INITIALIZER;
char *directory;

struct guideline {
	int version;
	void *blob;
	unsigned long length;
	char hash[33];
	struct guideline *next;
	struct guideline *prev;
} start_g, end_g;

void usage(char *progname);
int dir_poll(const char *d_name);
void *udp_server();
void *tcp_server();
void *poll_thread();
struct guideline *get_guideline_by_hash(const char *hash);
void clear_list();
int valid_filename(const char* filename);

int main(int argc, char *argv[])
{
	pthread_t poll_t, udp_t, tcp_t;
	int ret_poll, ret_udp, ret_tcp;


	if (argc != 2) usage(argv[0]);

	/*
	 * TODO: implement check on valid directory passed as argv[1]
	 */

	/* Initialise linked list */
	memset(&start_g, 0, sizeof(struct guideline));
	memset(&end_g, 0, sizeof(struct guideline));
	start_g.version = 0;
	start_g.blob = NULL;
	start_g.length = 0;
	start_g.next = &end_g;
	start_g.prev = NULL;

	end_g.version = 1000;
	end_g.blob = NULL;
	end_g.length = 0;
	end_g.next = NULL;
	end_g.prev = &start_g;

	directory = argv[1];

	printf("Polling %s at start-up\n", directory);
	if (dir_poll(directory)) {
		printf("Failed to poll %s\n", directory);
		exit(EXIT_FAILURE);
	}


	/* Start threads */
	if ((ret_udp = pthread_create(&udp_t, NULL, udp_server, NULL))) {
		printf("Failure in UDP thread\n");
		exit(EXIT_FAILURE);
	}

	if ((ret_tcp = pthread_create(&tcp_t, NULL, tcp_server, NULL))) {
		printf("Failure in TCP thread\n");
		exit(EXIT_FAILURE);
	}

	if ((ret_poll = pthread_create(&poll_t, NULL, poll_thread, NULL))) {
		printf("Failure in directory polling thread\n");
		exit(EXIT_FAILURE);
	}

	printf("stung appears to have started successfully\n");
	
	pthread_join(udp_t, NULL);
	pthread_join(tcp_t, NULL);
	pthread_join(poll_t, NULL);

	exit(EXIT_SUCCESS);
}

void usage(char *progname)
{
	printf("Usage: %s <directory-to-watch>\n", progname);
	exit(EXIT_FAILURE);
}

void *poll_thread()
{
	while(1) {
		int result = 0;
		pthread_mutex_lock(&file_mutex);
		result = dir_poll(directory);
		pthread_mutex_unlock(&file_mutex);
		if (result) break;
		sleep(3600);
	}

	printf("Polling has encountered an error and has terminated\n");
	return NULL;
}

int dir_poll(const char *d_name)
{
	DIR *poll_dir;
	struct dirent *entry;

	pthread_mutex_lock(&ll_mutex);
	clear_list();
	poll_dir = opendir(d_name);
	if(!poll_dir) {
		printf("Failed to open directory for polling\n");
		perror("dir_poll");
		pthread_mutex_unlock(&ll_mutex);
		return 1;
	}

	entry = readdir(poll_dir);
	while(entry){
		if(valid_filename(entry->d_name))
			printf("Found valid file: %s\n", entry->d_name);
		entry = readdir(poll_dir);
	}
	closedir(poll_dir);
	pthread_mutex_unlock(&ll_mutex);
	printf("dir_poll called\n");

	return 0;
}

void *udp_server()
{
	int server_fd, client_fd;
	socklen_t server_len, client_len;
	struct sockaddr_in server_ad, client_ad;
	ssize_t transmitted;
	int result;
	struct guideline *gp;
	char chall[UDP_CHALL_SIZE + 1];
	char resp[UDP_RESP_SIZE + 1];

	server_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (server_fd < 0) {
		printf("Failed to set udp socket\n");
		perror("server_fd");
		exit(EXIT_FAILURE);
	}
	memset(&server_ad, 0, sizeof(struct sockaddr_in));
	server_ad.sin_family = AF_INET;
	server_ad.sin_addr.s_addr = htonl(INADDR_ANY);
	server_ad.sin_port = htons(14935);
	server_len = sizeof(server_ad);
	if ((bind(server_fd, (struct sockaddr *)&server_ad, server_len)) < 0) {
		printf("Failed to bind udp socket\n");
		perror("server_fd");
		exit(EXIT_FAILURE);
	}

	chall[UDP_CHALL_SIZE] = 0;
	for (;;) {
		client_len = sizeof(client_ad);
		transmitted = recvfrom(server_fd, chall, UDP_CHALL_SIZE, 0,
				(struct sockaddr *) &client_ad, &client_len);
		if (transmitted != 3) {
			printf("Failed udp connection\n");
			continue;
		}

		result = atoi(chall);
		result = abs(result);
		if ((!result) || (result > 999)) {
			printf("Did not receive number challenge: %s\n", chall);
			continue;
		}
		printf("Received %d\n", result);

		pthread_mutex_lock(&ll_mutex);
		gp = start_g.next;
		if (gp == &end_g) {
			printf("Empty guideline list\n");
			strncpy(resp, "No guidelines found",
					UDP_RESP_SIZE +1);
		}
		else {
			while (!(gp->prev->version <= gp->version) &&
				(gp->next->version > gp->version)) {
			gp = gp->next;
			}
			strncpy(resp, gp->hash, UDP_RESP_SIZE +1);
		}
		pthread_mutex_unlock(&ll_mutex);

		client_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (client_fd < 0) {
			printf("Failed to create socket for udp client\n");
			perror("client_fd");
			continue;
		}
		if(sendto(client_fd, resp, UDP_RESP_SIZE, 0,
				(struct sockaddr *)&client_ad, 
				client_len) != UDP_RESP_SIZE) {
			printf("Failed to send udp response correctly\n");
			perror("sendto");
		}
		close(client_fd);
	}
}

void *tcp_server()
{
	int server_fd, client_fd;
	socklen_t server_len, client_len;
	struct sockaddr_in server_ad, client_ad;
	ssize_t transmitted;
	struct guideline *gp;
	char readbuf[TCP_READBUF_SIZE];
	ssize_t nread;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) {
		printf("Failed to set up udp socket\n");
		perror("server_fd");
		exit(EXIT_FAILURE);
	}

	memset(&server_ad, 0, sizeof(struct sockaddr_in));
	server_ad.sin_family = AF_INET;
	server_ad.sin_addr.s_addr = htonl(INADDR_ANY);
	server_ad.sin_port = htons(14935);
	server_len = sizeof(server_ad);
	if((bind(server_fd, (struct sockaddr *)&server_ad, server_len)) < 0) {
		printf("Failed to bind tcp socket\n");
		perror("server_fd");
		exit(EXIT_FAILURE);
	}
	if (listen(server_fd, 5) < 0) {
		printf("Failed to set listen backlog\n");
		perror("server_fd");
		exit(EXIT_FAILURE);
	}

	for (;;) {
		char *index;

		client_len = sizeof(client_ad);
		client_fd = accept(server_fd, (struct sockaddr *)&client_ad,
				&client_len);
		if (client_fd < 0) {
			printf("Error connecting\n");
			continue;
		}

		memset(readbuf, 0, TCP_READBUF_SIZE);
		nread = read(client_fd, readbuf, TCP_READBUF_SIZE -1);
		if (nread != (TCP_READBUF_SIZE -1)) {
			printf("Failed to read from client\n");
			close(client_fd);
			continue;
		}

		index = strstr(readbuf, "GET ");
		if (index != readbuf) {
			printf("TCP client did not request a GET\n");
			close(client_fd);
			continue;
		}
		index +=4;

		pthread_mutex_lock(&ll_mutex);
		gp = get_guideline_by_hash(index);
		if (!gp) {
			pthread_mutex_unlock(&ll_mutex);
			printf("TCP client did not request a valid file\n");
			/*
			 * TODO: ? inform client
			 */
			close(client_fd);
			continue;
		}
	}
}

/* Please lock ll_mutex before calling this */
struct guideline *get_guideline_by_hash(const char *hash)
{
	struct guideline *gp = start_g.next;
	while (gp != &end_g) {
		if(!strncmp(hash, gp->hash, 32)) return gp;
	}
	return NULL;
}

/* Please lock ll_mutex before calling this */
void clear_list()
{
	struct guideline* p = start_g.next;

	while(p) {
		struct guideline* old_p = p;
		p = old_p->next;
		if (p) {
			free(old_p->blob);
			free(old_p);
		}
	}
}

/* Please lock ll_mutex before calling this */
int add_guideline(int version, void *blob, unsigned long length, char *hash)
{
	struct guideline *p,*i;

	i = &start_g;

	while(!((i->version < version) && (i->next->version > version))) {
		if(i->version == version) break;
		i = i->next;
	}

	if((p = malloc(sizeof(struct guideline)))==NULL) {
		printf("Failed to allocate memory\n");
		printf("I do not know what to do, so terminating\n");
		exit(EXIT_FAILURE);
	}

	p->version = version;
	p->blob = blob;
	p->length = length;
	strncpy(p->hash, hash, 33);
	p->next = i;
	p->prev = i->prev;
	p->prev->next = p;
	i->prev = p;
}

int valid_filename(const char *filename)
{
	char *index;
	/*
	 * filename should be 38 characters
	 */
	if(strlen(filename) != 38) return 0;

	/*
	 * The final 3 characters should be ".gz"
	 */
	index = strstr(filename, ".gz");
	if (index != filename + 35) return 0;
	return 1;
}
