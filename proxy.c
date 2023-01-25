#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define BUF_SIZE 1024
#define NTHREADS  8
#define SBUFSIZE  5

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";

int all_headers_received(char *);
int parse_request(char *, char *, char *, char *, char *, char *);
void test_parser();
void print_bytes(unsigned char *, int);

int open_sfd(char *);
// void handle_client(int, char *, char *, char *, char *, char *, char *);
void *handle_client(void * vargp);

sbuf_t sbuf; /* Shared buffer of connected descriptors */


int main(int argc, char *argv[])
{
	struct sockaddr_storage peer_addr;
	socklen_t clientLen;
	pthread_t tid;

	// Open the socket in the desired local port
	int sfd = open_sfd(argv[1]);

	// Spin up 8 threads (with no args)
	sbuf_init(&sbuf, SBUFSIZE); //line:conc:pre:initsbuf
	for (int i = 0; i < NTHREADS; i++)  /* Create worker threads */
		pthread_create(&tid, NULL, handle_client, NULL);             


	for(;;) {
		int newSocket = -1;
		clientLen = sizeof(struct sockaddr_storage);
		printf("****Starting Client****\n");	
		newSocket = accept(sfd, (struct sockaddr *) &peer_addr, &clientLen);
		sbuf_insert(&sbuf, newSocket);
		// pthread_create(&tid, NULL, handle_client, newSocket);
		// handle_client(newSocket, request, method, hostname, port, path, headers);
		printf("****Finished Client****\n");
	}


	printf("%s\n", user_agent_hdr);
	return 0;
}

// void handle_client(int fd, char *request, char *method,
// 		char *hostname, char *port, char *path, char *headers) {
void *handle_client(void *vargp) {
	pthread_detach(pthread_self()); //line:conc:echoservert:detach

	while (1) {
		int fd = sbuf_remove(&sbuf);

		char request[MAX_OBJECT_SIZE];
		char method[128]; 
		char hostname[1024];
		char port[64];
		char path[1024];
		char headers[1024];

		char buf[5000];
		int nRead = 0;
		int totalRead = 0;
		char inBytes[1024];
		// Read from client *********
		for(;;) {

			nRead = recv(fd, inBytes, BUF_SIZE, 0);

			if (nRead == 0) {
				break;
			}

			memcpy(&buf[totalRead], inBytes, nRead);

			totalRead += nRead;


			if (strstr(buf, "\r\n\r\n") != NULL) {
				break;
			}
		}

		printf("Request: @@ \n%s\n", buf);

		// Add a null char
		char tempBuf[totalRead + 1];
		strncpy(tempBuf, buf, totalRead);
		tempBuf[totalRead] = '\0';

		// print_bytes(tempBuf, totalRead + 1);
		strncpy(request, tempBuf, totalRead);
		parse_request(request, method, hostname, port, path, headers);

		printf("Parsed Headers: \n%s\n", headers);

		// Create Request for server ***********
		// Check if port exists
		int hasPort = 1;
		if (strcmp("80", port) == 0) {
			hasPort = 0;
		}

		// creating request
		char serverRequest[5000];

		printf("This is the hostname: %s\n", hostname);
		if (hasPort) {
			sprintf(serverRequest, "%s /%s HTTP/1.0\r\nHost: %s:%s\r\nUser-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\nConnection: close\r\nProxy-Connection: close\r\n\r\n", 
							method, path, hostname, port);
		}
		else {
			sprintf(serverRequest, "%s /%s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\nConnection: close\r\nProxy-Connection: close\r\n\r\n", 
							method, path, hostname);
		}

		printf("Server Request: @@\n%s", serverRequest);

		// Send Request to Server *********
		int serverSocketFD;
		struct addrinfo hints;
		struct addrinfo *result;
		/* Obtain address(es) matching host/port */
		hints.ai_family = AF_INET;    /* Allow IPv4, IPv6, or both, depending on
									what was specified on the command line. */
		hints.ai_socktype = SOCK_STREAM; /* Datagram socket */
		hints.ai_flags = 0;
		hints.ai_protocol = 0;  /* Any protocol */

		getaddrinfo(hostname, port, &hints, &result);
		serverSocketFD = socket(result->ai_family, result->ai_socktype, 0);
		connect(serverSocketFD, result->ai_addr, result->ai_addrlen);

		send(serverSocketFD, serverRequest, strlen(serverRequest), 0);


		// Receive response from server ********
		char serverBuf[MAX_OBJECT_SIZE];
		int serverNRead = 0;
		int serverTotalRead = 0;
		char serverInBytes[MAX_OBJECT_SIZE];

		// Read from client *********
		for(;;) {

			serverNRead = recv(serverSocketFD, serverInBytes, BUF_SIZE, 0);

			if (serverNRead == 0) {
				break;
			}

			memcpy(&serverBuf[serverTotalRead], serverInBytes, serverNRead);

			serverTotalRead += serverNRead;
		}

		printf("Server Response: @@ \n%s\n", serverBuf);

		// Send server resposne to the client ********
		send(fd, serverBuf, serverTotalRead, 0);

		close(fd);
		close(serverSocketFD);
	}
	return NULL;
}

int open_sfd(char *port) {
// *************

	int s, sfd;

	struct addrinfo hints;
	struct addrinfo *result;

	/* Obtain address(es) matching host/port */
	hints.ai_family = AF_INET;    /* Allow IPv4, IPv6, or both, depending on
								what was specified on the command line. */
	hints.ai_socktype = SOCK_STREAM; /* Datagram socket */
	hints.ai_flags = 0;
	hints.ai_protocol = 0;  /* Any protocol */

	// /* SECTION A - pre-socket setup; getaddrinfo() */

	// /* getaddrinfo() returns a list of address structures.  However,
	//    because we have only specified a single address family (AF_INET or
	//    AF_INET6) and have only specified the wildcard IP address, there is
	//    no need to loop; we just grab the first item in the list. */
	if ((s = getaddrinfo(NULL, port, &hints, &result)) < 0) {
			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
			exit(EXIT_FAILURE);
	}


	// get addr info
	sfd = socket(result->ai_family, result->ai_socktype, 0);

	int optval = 1;
	setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

	// struct sockaddr_in temp;
	// temp.sin_port = htons(port);
	// temp.sin_family = AF_INET;
	// TODO: bind() it to a port passed as the first argument from the command line, and 
	// 		configure it for accepting new clients with listen()
	bind(sfd, result->ai_addr, result->ai_addrlen);

     listen(sfd, 100);

	return sfd;
}

int all_headers_received(char *request) {
	char* start = strstr(request, "\r\n\r\n");
	if (start == NULL) {
		return 0;
	}
	return 1;
}

int parse_request(char *request, char *method,
		char *hostname, char *port, char *path, char *headers) {
				
			int printTest = 0;

			printf("Entering parse_request\n");
			if (all_headers_received(request) == 1) {

				if (printTest) {
					printf("all_headers_received passed\n");
				}

				printf("request = \n%s\n", request);

				strncpy(method, request, (strlen(request) - strlen(strstr(request, " "))));

				if (printTest) {
					printf("Got Method: %s\n", method);
				}

				// Skip the "the Host:" tag
				char* remainingString = strstr(request, "Host:") + 6;


				// There is no port
				if (strlen(strstr(remainingString, ":")) < strlen(strstr(remainingString, "\r"))) {
					strncpy(hostname, remainingString, (strlen(remainingString) - strlen(strstr(remainingString, "\r"))));
					sprintf(port, "80");
					// port = "80";
				}
				
				else {
					// Get host name, remove the " " and add null charcter
					strncpy(hostname, remainingString, (strlen(remainingString) - strlen(strstr(remainingString, ":"))));
					hostname = hostname + 1;
					hostname[strlen(remainingString) - strlen(strstr(remainingString, ":")) - 1] = 0;


					if (printTest) {
						printf("Got hostname: %s\n", hostname);
					}

					// Move forward to port location
					remainingString = strstr(remainingString, ":");
					remainingString = strstr(remainingString, ":") + 1;

					// Get port and add null charcte
					strncpy(port, remainingString, (strlen(remainingString) - strlen(strstr(remainingString, "\r"))));
					port[strlen(remainingString) - strlen(strstr(remainingString, "\r"))] = 0;

					
					if (printTest) {
						printf("Got Port: %s\n", port);
					}
				}


				// Get the path (move forward over the string)
				remainingString = strstr(request, "//") + 2;
				remainingString = strstr(remainingString, "/") + 1;
				strncpy(path, remainingString, (strlen(remainingString) - strlen(strstr(remainingString, " "))));
				// Add the null charcter	
				path[strlen(remainingString) - strlen(strstr(remainingString, " "))] = 0;

				
				if (printTest) {
					printf("Got Path: %s\n", path);
				}

				// Get the remaining headers
				strncpy(headers, strstr(request, "Host:"), ((strlen(strstr(request, "Host:")))));
				// Add the null charcter
				headers[strlen(strstr(request, "Host:"))] = 0;

				
				if (printTest) {
					printf("Got remaining headers\n");
				}

				fflush(stdout);


			printf("parse_request success\n");

			}

			else {
				printf("Headers Not Received\n");
				return 0;
			}
	return 0;
}

void test_parser() {
	int i;
	char method[16], hostname[64], port[8], path[64], headers[1024];

       	char *reqs[] = {

		"GET http://www.example.com:8080/index.html HTTP/1.0\r\n",

		"GET http://www-notls.imaal.byu.edu:5599/cgi-bin/slowsend.cgi?obj=lyrics HTTP/1.1\r\n"
		"Host: www-notls.imaal.byu.edu:5599\r\n"
		"User-Agent: curl/7.68.0\r\n"
		"Accept: */*\r\n"
		"Proxy-Connection: Keep-Alive\r\n\r\n",
		
		"GET http://www.example.com/index.html HTTP/1.0\r\n"
		"Host: www.example.com\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://www.example.com:8080/index.html?foo=1&bar=2 HTTP/1.0\r\n"
		"Host: www.example.com:8080\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://localhost:15487/home.html HTTP/1.1"
		"Host: localhost:15487"
		"User-Agent: curl/7.68.0"
		"Accept: */*"
		"Proxy-Connection: Keep-Alive\r\n\r\n",

		NULL
	};
	
	for (i = 0; reqs[i] != NULL; i++) {
		printf("Testing %s\n", reqs[i]);
		if (parse_request(reqs[i], method, hostname, port, path, headers)) {
			printf("METHOD: %s\n", method);
			printf("HOSTNAME: %s\n", hostname);
			printf("PORT: %s\n", port);
			printf("HEADERS: %s\n", headers);
		} else {
			printf("REQUEST INCOMPLETE\n");
		}
	}
}

void print_bytes(unsigned char *bytes, int byteslen) {
	int i, j, byteslen_adjusted;

	if (byteslen % 8) {
		byteslen_adjusted = ((byteslen / 8) + 1) * 8;
	} else {
		byteslen_adjusted = byteslen;
	}
	for (i = 0; i < byteslen_adjusted + 1; i++) {
		if (!(i % 8)) {
			if (i > 0) {
				for (j = i - 8; j < i; j++) {
					if (j >= byteslen_adjusted) {
						printf("  ");
					} else if (j >= byteslen) {
						printf("  ");
					} else if (bytes[j] >= '!' && bytes[j] <= '~') {
						printf(" %c", bytes[j]);
					} else {
						printf(" .");
					}
				}
			}
			if (i < byteslen_adjusted) {
				printf("\n%02X: ", i);
			}
		} else if (!(i % 4)) {
			printf(" ");
		}
		if (i >= byteslen_adjusted) {
			continue;
		} else if (i >= byteslen) {
			printf("   ");
		} else {
			printf("%02X ", bytes[i]);
		}
	}
	printf("\n");
}


// Stuff from sbuf.c
// $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
/* Create an empty, bounded, shared FIFO buffer with n slots */
/* $begin sbuf_init */
void sbuf_init(sbuf_t *sp, int n)
{
    sp->buf = calloc(n, sizeof(int)); 
    sp->n = n;                      /* Buffer holds max of n items */
    sp->front = sp->rear = 0;        /* Empty buffer iff front == rear */
    sem_init(&sp->mutex, 0, 1);      /* Binary semaphore for locking */
    sem_init(&sp->slots, 0, n);      /* Initially, buf has n empty slots */
    sem_init(&sp->items, 0, 0);      /* Initially, buf has zero data items */
}
/* $end sbuf_init */

/* Clean up buffer sp */
/* $begin sbuf_deinit */
void sbuf_deinit(sbuf_t *sp)
{
    free(sp->buf);
}
/* $end sbuf_deinit */

/* Insert item onto the rear of shared buffer sp */
/* $begin sbuf_insert */
void sbuf_insert(sbuf_t *sp, int item)
{
    printf("insert: before wait:slots\n");
    sem_wait(&sp->slots);                          /* Wait for available slot */
    printf("insert: after wait:slots\n");

    sem_wait(&sp->mutex);                          /* Lock the buffer */
    sp->buf[(++sp->rear)%(sp->n)] = item;   /* Insert the item */
    sem_post(&sp->mutex);                          /* Unlock the buffer */
    
    printf("insert: before post:items\n");
    sem_post(&sp->items);                          /* Announce available item */
    printf("insert: after post:items\n");
}
/* $end sbuf_insert */

/* Remove and return the first item from buffer sp */
/* $begin sbuf_remove */
int sbuf_remove(sbuf_t *sp)
{
    int item;
    printf("remove: before wait:items\n");
    sem_wait(&sp->items);                          /* Wait for available item */
    printf("remove: after wait:items\n");

    sem_wait(&sp->mutex);                          /* Lock the buffer */
    item = sp->buf[(++sp->front)%(sp->n)];  /* Remove the item */
    sem_post(&sp->mutex);                          /* Unlock the buffer */
    
    printf("remove: before post:slots\n");
    sem_post(&sp->slots);                          /* Announce available slot */
    printf("remove: after post:slots\n");
    
    return item;
}
/* $end sbuf_remove */
/* $end sbufc */