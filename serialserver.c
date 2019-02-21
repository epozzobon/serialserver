#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <termios.h>

#define PT_NAME_MAX_LEN 255


struct server_data {
	int socket;
	uint16_t port;
	pthread_t thread;
};

struct client_data {
	int socket;
	int pt;
	pthread_t thread;
	pthread_t thread_t2s;
	pthread_t thread_s2t;
    char pt_name[PT_NAME_MAX_LEN+1];
};


void *tcp_to_serial(void *arg) {
	struct client_data *ctx = arg;
	int ret, len;
	uint8_t buf[100];

	while (1) {
		ret = read(ctx->socket, buf, 100);
		if (ret < 0) {
			perror("socket read");
			break;
		}
		if (ret == 0) {
			printf("Socket %d disconnected\n", ctx->socket);
			break;
		}
		len = ret;
		ret = write(ctx->pt, buf, len);	
		if (ret < 0) {
			perror("pt write");
			continue;
		}
		if (ret < len) {
			printf("Socket %d could not be written to pt %s\n", ctx->socket, ctx->pt_name);
			break;
		}
		printf("Wrote %d bytes from socket %d to pt %s\n", ret, ctx->socket, ctx->pt_name);
	}

	printf("Socket %d for pt %s finished\n", ctx->socket, ctx->pt_name);
}


void *serial_to_tcp(void *arg) {
	struct client_data *ctx = arg;
	int ret, len;
	uint8_t buf[100];

	while (1) {
		ret = read(ctx->pt, buf, 100);
		if (ret < 0) {
			usleep(20);
			continue;
		}
		if (ret == 0) {
			printf("pt %s died\n", ctx->pt_name);
			break;
		}
		len = ret;
		ret = write(ctx->socket, buf, len);	
		if (ret < 0) {
			perror("socket write");
			break;
		}
		if (ret < len) {
			printf("pt %s could not be written to socket %d\n", ctx->pt_name, ctx->socket);
			break;
		}
		printf("Wrote %d bytes from pt %s to socket %d\n", ret, ctx->pt_name, ctx->socket);
	}
	
	printf("pt %s for socket %d finished\n", ctx->pt_name, ctx->socket);
}

void *emulate_serial(void *arg) {
	struct client_data *ctx = arg;
	struct termios termios_s;
	int ret;

	ret = posix_openpt(O_RDWR);
	if (ret < 0) {
		perror("posix_openpt");
		exit(1);
	}
	ctx->pt = ret;

	ret = grantpt(ctx->pt);
	if (ret < 0) {
		perror("grantpt");
		exit(1);
	}

	ret = unlockpt(ctx->pt);
	if (ret < 0) {
		perror("unlockpt");
		exit(1);
	}

	char *ptr = ptsname(ctx->pt);
	if (ptr == NULL) {
		perror("ptsname");
		exit(1);
	}

	ret = tcgetattr(ctx->pt, &termios_s);
	if (ret != 0) {
		perror("tcgetattr");
		exit(1);
	}

	termios_s.c_lflag &= ~ECHO;

	ret = tcsetattr(ctx->pt, TCSANOW, &termios_s);
	if (ret != 0) {
		perror("tcgetattr");
		exit(1);
	}

    strncpy(ctx->pt_name, ptr, PT_NAME_MAX_LEN);
    // Termination in case the name was too long
    ctx->pt_name[PT_NAME_MAX_LEN] = 0;

	printf("Mirroring socket %d at pt %s\n", ctx->socket, ctx->pt_name);
	
	ret = pthread_create(&ctx->thread_s2t, NULL, serial_to_tcp, ctx);
	if (ret) {
		fprintf(stderr, "pthread_create returned %d\n", ret);
		exit(1);
	}

	ret = pthread_create(&ctx->thread_t2s, NULL, tcp_to_serial, ctx);
	if (ret) {
		fprintf(stderr, "pthread_create returned %d\n", ret);
		exit(1);
	}
	
	pthread_join(ctx->thread_t2s, NULL);
	close(ctx->pt);
	close(ctx->socket);
	pthread_join(ctx->thread_s2t, NULL);

	free(ctx);
}


void *accept_clients(void *arg) {
	struct server_data *ctx = arg;

	printf("Accepting clients on socket %d\n", ctx->socket);

	while (1) {
		struct sockaddr_in caddr;
		socklen_t len = sizeof(caddr);
		int cs = accept(ctx->socket, (struct sockaddr *) &caddr, &len);	
		if (cs < 0) {
			perror("accept");
			exit(1);
		}

		struct client_data *client;
		client = malloc(sizeof(*client));
		if (client == NULL) {
			perror("malloc");
			exit(1);
		}
		client->socket = cs;

		int ret = pthread_create(&client->thread, NULL, emulate_serial, client);
		if (ret) {
			fprintf(stderr, "pthread_create returned %d\n", ret);
			exit(1);
		}
	}
}


int open_socket(struct server_data *ctx) {
	int ret;
	int ss;

	ss = socket(AF_INET, SOCK_STREAM, 0);
	if (ss < 0) {
		perror("socket");
		exit(1);
	}
	ctx->socket = ss;

	struct sockaddr_in saddr = { 
		.sin_family = AF_INET,
		.sin_port = htons(ctx->port),
	};
	ret = bind(ss, (struct sockaddr *) &saddr, sizeof(saddr));
	if (ret < 0) {
		perror("bind");
		exit(1);
	}

	ret = listen(ss, 1);
	if (ret < 0) {
		perror("listen");
		exit(1);
	}

	ret = pthread_create(&ctx->thread, NULL, accept_clients, ctx);
	if (ret) {
		fprintf(stderr, "pthread_create returned %d\n", ret);
		exit(1);
	}


	return ss;
}


int main(int argc, char *argv[]) {
	int ret;

	// TODO: use ports given in argv
	struct server_data s1 = { .port = 62938 };
	struct server_data s2 = { .port = 62979 };
	ret = open_socket(&s1);
	ret = open_socket(&s2);
	pthread_join(s1.thread, NULL);
	pthread_join(s2.thread, NULL);
	close(s1.socket);
	close(s2.socket);

	return 0;
}

