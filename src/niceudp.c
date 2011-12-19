#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
char *directory;

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

int main(int argc, char *argv[])
{
	pthread_t poll_t, udp_t, tcp_t;
	int ret_poll, ret_udp, ret_tcp;

	if (argc != 2) usage(argv[0]);

	/*
	 * TODO: implement check on valid directory passed as argv[1]
	 */
	directory = argv[1];
	printf("Polling %s at start-up\n", directory);
	if (dir_poll()) {
		printf("Failed to poll %s\n", directory);
		exit(EXIT_FAILURE);
	}

	/* Start threads */
	if (ret_udp = pthread_create(&udp_t, NULL, udp_server, NULL)) {
		printf("Failure in UDP thread\n");
		exit(EXIT_FAILURE);
	}

	if (ret_tcp = pthread_create(&tcp_t, NULL, tcp_server, NULL)) {
		printf("Failure in TCP thread\n");
		exit(EXIT_FAILURE);
	}

	if (ret_poll = pthread_create(&tcp_t, NULL, poll_thread, NULL)) {
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
	printf("dir_poll called\n");

	return 0;
}

void *udp_server()
{
	printf("udp_server thread ok\n");
}

void *tcp_server()
{
	printf("tcp_server thread ok\n");
}
