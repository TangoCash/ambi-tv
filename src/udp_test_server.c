#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define PORT     21324
#define MTU      1474

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

		uint16_t led = ((buffer[3] << 0) & 0xFF) + ((buffer[2] << 8) & 0xFF00);
		unsigned int count = (n - 4) / 3;
		printf("Led index start from %d count %d\n", led, count);

		unsigned char *payload = &buffer[4];

		for (; count > 0; count--, payload += 3)
		{
			printf("<LED %d 0x%02x : 0x%02x : 0x%02x> ", led, payload[0], payload[1], payload[2]);
			led++;
		}

		printf("\n");
	}

	return 0;
}
