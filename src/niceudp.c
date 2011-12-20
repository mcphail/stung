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

#define UDP_CHALL_SIZE 3
#define UDP_RESP_SIZE 32

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

/*
int main()
{
	int server_sockfd, client_sockfd;
	int server_len, client_len;
	struct sockaddr_in server_address;
	struct sockaddr_in client_address;
	int result;
	fd_set readfds, testfds;

	server_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);
	server_address.sin_port = htons(9734);
	server_len = sizeof(server_address);

	bind(server_sockfd, (struct sockaddr *)&server_address, server_len);


	FD_ZERO(&readfds);
	FD_SET(server_sockfd, &readfds);

	while(1) {
		char ch;
		int fd;
		int nread;

		testfds = readfds;

		printf("server waiting\n");
		result = select(FD_SETSIZE, &testfds, (fd_set *)0,
				(fd_set *)0,
				(struct timeval *)0);
		
		if(result < 1) {
			perror("nmpserver");
			exit(1);
		}

		for(fd = 0; fd < FD_SETSIZE; fd++) {
			if(FD_ISSET(fd,&testfds)) {
				if(fd == server_sockfd) {
					client_len = sizeof(client_address);
					client_sockfd = accept(server_sockfd,
							(struct sockaddr *)&client_address,
							&client_len);
					FD_SET(client_sockfd, &readfds);
					printf("adding client on fd %d\n",
							client_sockfd);
				}
				else {
					read(fd, &ch, 1);
					sleep(5);
					printf("serving client on fd %d\n",
							fd);
					ch++;
					write(fd, &ch, 1);
				}
			}
		}
	}
}
*/

void usage(char *progname);
int dir_poll();
void *udp_server();
void *tcp_server();
void *poll_thread();

void clear_list();

int main(int argc, char *argv[])
{
	pthread_t poll_t, udp_t, tcp_t;
	int ret_poll, ret_udp, ret_tcp;


	if (argc != 2) usage(argv[0]);

	/*
	 * TODO: implement check on valid directory passed as argv[1]
	 */

	/* Initialise linked list */
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
	if (dir_poll()) {
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
		result = dir_poll();
		pthread_mutex_unlock(&file_mutex);
		if (result) break;
		sleep(3600);
	}

	printf("Polling has encountered an error and has terminated\n");
}

int dir_poll()
{
	/*
	 * TODO: something useful
	 */
	pthread_mutex_lock(&ll_mutex);
	clear_list();
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

	printf("udp_server thread ok\n");

	server_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (server_fd < 0) {
		printf("Failed to set udp socket\n");
		perror("server_fd");
		exit(EXIT_FAILURE);
	}
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

		result = (int) strtod(chall, NULL);
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
		if(sendto(client_fd, resp, UDP_RESP_SIZE, 0,
				(struct sockaddr *)&client_ad, 
				client_len) != UDP_RESP_SIZE) {
			printf("Failed to send udp response correctly\n");
			perror("sendto");
		}
	}
}

void *tcp_server()
{
	printf("tcp_server thread ok\n");
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
