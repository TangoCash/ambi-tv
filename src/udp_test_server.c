#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define PORT     19446
#define MTU      1492

int main()
{
	int sockfd;
	unsigned char buffer[MTU];
	struct sockaddr_in servaddr, cliaddr;

	// Creating socket file descriptor
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	memset(&servaddr, 0, sizeof(servaddr));
	memset(&cliaddr, 0, sizeof(cliaddr));
	// Filling server information
	servaddr.sin_family    = AF_INET; // IPv4
	servaddr.sin_addr.s_addr = INADDR_ANY;
	servaddr.sin_port = htons(PORT);

	// Bind the socket with the server address
	if (bind(sockfd, (const struct sockaddr *)&servaddr,
			sizeof(servaddr)) < 0)
	{
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	unsigned int   len, n;
	
	while (1)
	{
		len = sizeof(cliaddr);  //len is value/result
		n = recvfrom(sockfd, (unsigned char *)buffer, MTU, 0, (struct sockaddr *) &cliaddr, &len);

		printf("Received : %d bytes\n", n);
		printf("Mode : %x\n", buffer[0]);
		printf("Wait : %x\n", buffer[1]);
		
		unsigned int led = (buffer[2] << 8) + buffer[3];
		unsigned int count = (n-4) / 3;
		printf("Leds start from %d to %d\n", led, led+count);
		
		unsigned char* payload = &buffer[4];

		for (; count > 0; count--, payload+=3, led++)
			printf("<LED %d 0x%02x : 0x%02x : 0x%02x> ", led, payload[0], payload[1], payload[2]);

		printf("\n");
	}

	return 0;
}
