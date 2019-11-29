#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>

#define OPCODE_RRQ		1
#define OPCODE_WRQ		2
#define OPCODE_DATA		3
#define OPCODE_ACK		4
#define OPCODE_ERROR	5
#define OPCODE_OACK		6

#define BLOCK_SIZE		65464
#define BUFFER_SIZE		BLOCK_SIZE + 2 // car il faut 2 octets pour le opcode + 2 octects pour le block id + BLOCK_SIZE octets pour les data

void putFile(char *fileName, struct sockaddr *sock_addr, int sockfd) {
/*
2 bytes     string    1 byte     string   1 byte
------------------------------------------------
| Opcode |  Filename  |   0  |    Mode    |   0  |   blksize  |  0  |   XXXXX   |  0  |
------------------------------------------------
ici on va avoir un mode = "octet" donc il faut faire +5 
Il y a aussi le "blksize" il faut donc 7 octets de plus
puis la valeur sur 2 octets
*/
	int requestLength = (int)(2 + strlen(fileName) + 1 + 5 + 1 + 7 + 1);
	char  buffer[BUFFER_SIZE];
	int sendSize;
	int recvSize;
	char opcode;
	int readFileSize;
	socklen_t addrSize;
	unsigned short blockId; //unsigned short car 2 octets
	unsigned short blockCounter = 0;
	FILE  *fileIn;

	struct sockaddr sock_in_addr;

	sprintf(buffer, "%d", BLOCK_SIZE);
	requestLength += strlen(buffer);
	requestLength += 1;

	buffer[0] = 0;
	buffer[1] = OPCODE_WRQ;
	strcpy(&buffer[2], fileName);
	strcpy(&buffer[2 + strlen(fileName) + 1 ], "octet");
	strcpy(&buffer[2 + strlen(fileName) + 1 + strlen("octet") + 1 ], "blksize");
	sprintf(&buffer[2 + strlen(fileName) + 1 + strlen("octet") + 1 + strlen("blksize") + 1 ], "%d" ,BLOCK_SIZE);

	int c = 0;
	printf("| ");
	while (buffer[c] != '\0' || c < requestLength) {
		if(c < 2) {
			printf("%d", buffer[c]);
		} else if(buffer[c] == 0) {
			printf(" | 0 | ");
		} else {
			printf("%c", buffer[c]);
		}
		c++;
	}

	printf("\n");

	if ((fileIn = fopen(fileName, "r")) == NULL) {
		printf("Error : unable to open file %s\n", fileName);
		return;
	}


	sendSize = sendto(sockfd, buffer, requestLength, 0, sock_addr, sizeof(struct sockaddr));
	if (sendSize == -1 || sendSize != requestLength) {
		printf("error while sendto: %d\n", sendSize);
		fclose(fileIn);
		return;
	}

	addrSize = sizeof(struct sockaddr);
	recvSize = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, &sock_in_addr, &addrSize);
	if (recvSize == -1) {
		printf("error while recvfrom: %d\n", recvSize);
		fclose(fileIn);
		return;
	}
	opcode = buffer[1];
	if(opcode == OPCODE_ERROR) {
		printf("error opcode %s\n", &buffer[4]);
		fclose(fileIn);
		return;
	} else if(opcode != OPCODE_OACK) {
		printf("error opcode %d\n",opcode);
		fclose(fileIn);
		return;
	}

	printf("received ACK, sending file\n");

	
	blockCounter = 1;

	while(1) {
		readFileSize = fread(&buffer[4], 1, BLOCK_SIZE, fileIn);
		if(readFileSize <= 0) {
			printf("error fread %d\n",readFileSize);
			fclose(fileIn);
			return;
		}
		buffer[0] = 0;
		buffer[1] = OPCODE_DATA;
		buffer[2] = (unsigned char)(blockCounter>>8)&0xff;
		buffer[3] = (unsigned char)(blockCounter&0xff);


		sendSize = sendto(sockfd, buffer, readFileSize + 4, 0, &sock_in_addr, sizeof(struct sockaddr));
		if (sendSize == -1 || sendSize != (readFileSize + 4)) {
			printf("error while sendto: %d %d\n", sendSize, readFileSize+4);
			fclose(fileIn);
			return;
		}
		printf("block %d sended with %d bytes\n", blockCounter, readFileSize);


		//ACK
		addrSize = sizeof(struct sockaddr);
		recvSize = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, &sock_in_addr, &addrSize);
		if (recvSize == -1) {
			printf("error while recvfrom: %d\n", recvSize);
			fclose(fileIn);
			return;
		}
		opcode = buffer[1];
		if(opcode == OPCODE_ERROR) {
			printf("error opcode %s\n", &buffer[4]);
			fclose(fileIn);
			return;
		}
		
		blockId = (unsigned char)buffer[2]<<8 | (unsigned char)buffer[3];

		if(opcode != OPCODE_ACK /*|| blockId != blockCounter*/) {
			printf("error opcode %d blockid: %d blockcounter: %d buff2: %x buff3: %x\n", opcode, blockId, blockCounter, (unsigned char)buffer[2], (unsigned char)buffer[3]);
			fclose(fileIn);
			return;
		}
		blockCounter++;
		if(readFileSize != BLOCK_SIZE) {
			fclose(fileIn);
			return;
		}
	}
}


int main(int argc, char *args[]) {
	int s;
    struct addrinfo hints;
    struct addrinfo *res;
	char *token;
	char *host[2];
	char ipbuf[INET_ADDRSTRLEN];
	int sockfd = 0;;

	if(argc != 3) {
		printf("Usage: putftp <host>:<port> <local file>\n");
		return 0;
	}

	memset(&hints,0,sizeof(struct addrinfo));

	//On va indiquer le type d'adresse que l'on souhaite avoir
    hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;


	host[1] = "69";//Si le port n'est pas déterminé, on utilise le port 69 par défaut	

	//On regarde si il y a un port en séparent l'host sur les :
	token = strtok(args[1], ":");
	while(token != NULL) {
		host[sockfd++] = token;
		token = strtok(NULL, ":");
		if(sockfd == 2) {
			break;
		}
	}

	printf("host: %s on port: %s, file: %s\n",host[0],host[1], args[2]);
	
	if ((s = getaddrinfo(host[0], host[1], &hints, &res)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
    }

	//Unquement pour vérifier, on affiche en format compréhensible par nous
	inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, ipbuf, sizeof(ipbuf));
    printf("connecting to server : %s \n", ipbuf);

	//On ouvre le socket pour communiquer avec le serveur
	if ((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1) {
        printf("error while creating socket\n");
    }
	
	putFile(args[2], res->ai_addr, sockfd);

	close(sockfd);
	freeaddrinfo(res);
}
