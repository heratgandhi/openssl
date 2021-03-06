#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include "c37.h"

#include <ctype.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <tcpr/types.h>

#define DFL_PORT	3350

//OpenSSL
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>

#define CERT_FILE "TrustStore.pem"
#define PRI_KEY "privatekey.key"
//////////

//OpenSSL
extern char  __data_start, __bss_start,_edata,_end;
BIO * bio;
SSL * ssl;
SSL_CTX * ctx;

int password_callback(char *buf, int size, int rwflag, void *userdata)
{
    /* For the purposes of this demonstration, the password is "ibmdw" */
    printf("*** Callback function called\n");
    strcpy(buf, "ibmdw");
    return 1;
}

int (*callback)(char *, int, int, void *) = &password_callback;

static int resolve_address(struct sockaddr_in *addr, const char *host,
			   const char *port)
{
	struct addrinfo hints;
	struct addrinfo *ai;
	int err;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	err = getaddrinfo(host, port, &hints, &ai);
	if (err)
		return err;

	memcpy(addr, ai->ai_addr, ai->ai_addrlen);
	freeaddrinfo(ai);
	return 0;
}

static int connect_to_tcpr(struct sockaddr_in *tcpraddr)
{
	int s;

	s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s < 0)
		return -1;

	if (connect(s, (struct sockaddr *)tcpraddr, sizeof(*tcpraddr)) < 0) {
		printf("Connect failed!!!\n");
		close(s);
		return -1;
	}

	return s;
}

static int get_tcpr_state(struct tcpr_ip4 *state, int tcprsock,
			  struct sockaddr_in *peeraddr, uint16_t bindport)
{
	memset(state, 0, sizeof(*state));
	state->peer_address = peeraddr->sin_addr.s_addr;
	state->tcpr.hard.peer.port = peeraddr->sin_port;
	state->tcpr.hard.port = htons(bindport);

	if (send(tcprsock, state, sizeof(*state), 0) < 0) {
		printf("send failed!!!\n");
		return -1;
	}
	if (recv(tcprsock, state, sizeof(*state), 0) < 0) {
		printf("recv failed!!!\n");
		return -1;
	}
	return 0;
}

//////////

/* Global stuff gleaned from program arguments.
 */
struct prog_args {
	char *name;
	char *port;
} prog_args;

static void usage(){
	fprintf(stderr, "Usage: %s [args]\n", prog_args.name);
	fprintf(stderr, "Optional argument:\n");
	fprintf(stderr, "	-p port: TCP server port [default = %d]\n", DFL_PORT);
	exit(1);
}

int do_copy(int fd){
	FILE *input = fopen("out.0230.dat", "r");
	FILE *output = fdopen(fd, "w");
	int first = 1;
	struct timeval tv;
	double start, firsttime;

	/* Figure out the start time.
	 */
	gettimeofday(&tv, 0);
	start = tv.tv_sec + (double) tv.tv_usec / 1000000;

	/* Copy input.
	 */
	while (!feof(input)) {
		/* Read one frame.
		 */
		char buf[FRAME_SIZE];
		int n = fread(buf, FRAME_SIZE, 1, input);
		if (n == 0) {
			break;
		}
		if (n < 0) {
			perror("do_copy: fread");
			exit(1);
		}

		/* Convert the frame.
		 */
		c37_packet *pkt = get_c37_packet(buf);
		if (pkt == 0) {
			fprintf(stderr, "%s: do_copy: bad packet\n", prog_args.name);
			exit(1);
		}
		if (pkt->framesize != FRAME_SIZE) {
			fprintf(stderr, "%s: do_copy: bad frame size\n", prog_args.name);
			exit(1);
		}

		/* If it's the first frame, remember its time.
		 */
		if (first) {
			firsttime = pkt->soc +
							(double) (pkt->fracsec & 0xFFFFFF) / 0x1000000;
			first = 0;
		}

		/* Get the time of the packet
		 */
		double pkttime = pkt->soc +
						(double) (pkt->fracsec & 0xFFFFFF) / 0x1000000;

		/* Get the current time.
		 */
		gettimeofday(&tv, 0);
		double now = tv.tv_sec + (double) tv.tv_usec / 1000000;

		/* See if we need to wait.
		 */
		double wait = start + (pkttime - firsttime) - now;
		int usec = (int) (wait * 1000000);
		if (usec > 0) {
			fflush(output);
			usleep(usec);
		}

		/* Update the time.
		 */
		double newtime = start + (pkttime - firsttime);
		pkt->soc = (int) newtime;
		pkt->fracsec = (pkt->fracsec & 0xFF000000) |
	 						(int) ((newtime - pkt->soc) * 1000000);

		write_c37_packet(output, pkt);
		free(pkt);
	}

	fclose(input);
	fclose(output);

	return 1;
}

void do_recv(int s){
	int fd;
	int yes = 1;
	int ns = 1;
	
	struct sockaddr_in pulladdr;
	int tcprsock;
	struct tcpr_ip4 state;
	
	for (;;) {
		if (listen(s, 1) < 0) {
			perror("listen");
			exit(1);
		}
		printf("Waiting for connection...\n");
		if ((fd = accept(s, 0, 0)) < 0) {
			perror("accept");
			exit(1);
		}
		
		setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
		//Default port for tcpr = 10000 and ip address is same as pulladdr
		resolve_address(&pulladdr, "10.0.0.1", "10000");
		//printf("Connecting to TCPR.\n");
		tcprsock = connect_to_tcpr(&pulladdr);
		if (tcprsock < 0) {
			perror("Connecting to TCPR");
			exit(EXIT_FAILURE);
		}

		printf("Waiting for existing master, if any.\n");
		if (get_tcpr_state(&state, tcprsock, &pulladdr, 10000) < 0) {
			perror("Getting TCPR state");
			exit(EXIT_FAILURE);
		}
		
		//OpenSSL
		SSL_set_fd(ssl, fd);		
		
		state.tcpr.hard.ack = htonl(ntohl(state.tcpr.hard.ack) + ns);
		if (send(tcprsock, &state, sizeof(state), 0) < 0)
		{
			printf("Error in sending.\n");
			return;
		}
		else
			printf("Sent ack\n");
		
		printf("Accept: %d\n",SSL_accept(ssl));
		
		state.tcpr.hard.ack = htonl(ntohl(state.tcpr.hard.ack) + ns);
		if (send(tcprsock, &state, sizeof(state), 0) < 0)
		{
			printf("Error in sending.\n");
			return;
		}
		else
			printf("Sent ack\n");
		
		////////////
		
		printf("Got connection...\n");
		
		while (do_copy(fd))
			;
		printf("Connection closed...\n");
	}
}

static void get_args(int argc, char *argv[]){
	prog_args.name = argv[0];

	int c;
	while ((c = getopt(argc, argv, "p:")) != -1) {
		switch (c) {
			case 'p':
				if (prog_args.port != 0) {
					fprintf(stderr, "%s: can specify only one port\n", prog_args.name);
					exit(1);
				}
				if ((prog_args.port = optarg) == 0) {
					fprintf(stderr, "%s: -p takes a port argument\n", prog_args.port);
					exit(1);
				}
				break;
			case '?':
			default:
				usage();
		}
	}

	/* Get the remaining args.
	 */
	if (argc - optind != 0) {
		usage();
	}
}

/* If called without arguments, listen for connections.  Otherwise make a
 * connection to the specified first argument.
 */
int main(int argc, char *argv[]){
	get_args(argc, argv);
	
	//OpenSSL
	SSL_load_error_strings();
	ERR_load_BIO_strings();
	ERR_load_SSL_strings();
	SSL_library_init();
	OpenSSL_add_all_algorithms();
	/////////

	int port = DFL_PORT;
	if (prog_args.port != 0) {
		if ((port = atoi(prog_args.port)) <= 0) {
			fprintf(stderr, "%s: port must be positive integer\n", prog_args.name);
			exit(1);
		}
	}

	/* Create and bind the socket.
	 */
	int s;
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(1);
	}

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("bind");
		exit(1);
	}
	
	//OpenSSL
	printf("Attempting to create SSL context... ");
	ctx = SSL_CTX_new(SSLv23_server_method());

	if(ctx == NULL)
	{
		printf("Failed. Aborting.\n");
		return 0;
	}

	printf("\nLoading certificates...\n");
	SSL_CTX_set_default_passwd_cb(ctx, callback);
	if(!SSL_CTX_use_certificate_file(ctx, CERT_FILE, SSL_FILETYPE_PEM))
	{
		ERR_print_errors_fp(stdout);
		SSL_CTX_free(ctx);
		return 0;
	}
	if(!SSL_CTX_use_PrivateKey_file(ctx, PRI_KEY, SSL_FILETYPE_PEM))
	{
		ERR_print_errors_fp(stdout);
		SSL_CTX_free(ctx);
		return 0;
	}
	ssl = SSL_new(ctx);
	////////////

	do_recv(s);
	return 0;
}
