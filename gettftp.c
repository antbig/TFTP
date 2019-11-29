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
#define BUFFER_SIZE		BLOCK_SIZE + 4 // car il faut 2 octets pour le opcode + 2 octects pour le block id + BLOCK_SIZE octets pour les data

void getFile(char *fileName, struct sockaddr *sock_addr, int sockfd) {
/*
2 bytes     string    1 byte     string   1 byte
------------------------------------------------
| Opcode |  Filename  |   0  |    Mode    |   0  |   blksize  |  0  |   XXXXX   |  0  |
------------------------------------------------
ici on va avoir un mode = "octet" donc il faut faire +5 
Il y a aussi le "blksize" il faut donc 7 octets de plus
puis la valeur sur 2 octets
*/
	int requestLength = (int)(2 + strlen(fileName) + 1 + 5 + 1 + 7 + 1);//La taille du packet à envoyer en octets
	char  buffer[BUFFER_SIZE];//Le buffer de données utilisé pour envoyer et recevoir
	int sendSize; //Variable utilisé pour savoir le nombre d'octet envoyé
	int recvSize;//Variable utilisé pour savoir le nombre d'octet reçus
	char opcode;//Le code permetant 
	socklen_t addrSize;
	unsigned short blockId; //unsigned short car 2 octets
	unsigned short blockCounter = 0;
	FILE  *fileOut;

	struct sockaddr sock_in_addr;

	sprintf(buffer, "%d", BLOCK_SIZE);
	requestLength += strlen(buffer);
	requestLength += 1;


	buffer[0] = 0;
	buffer[1] = OPCODE_RRQ;//On commence par effectuer une demande de lecture d'un fichier
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
	

	if ((fileOut = fopen(fileName, "w+")) == NULL) {
		printf("Error : unable to open file %s\n", fileName);
		return;
	}

	//On envoie le packet
	sendSize = sendto(sockfd, buffer, requestLength, 0, sock_addr, sizeof(struct sockaddr));
	if (sendSize == -1 || sendSize != requestLength) {// 2 moyen de détecter les erreurs, soit la fonction sendto retourne -1 soit le nombre d'octet envoyé ne correspond pas à se que l'on a demandé
		printf("error while sendto: %d\n", sendSize);
		fclose(fileOut);
		return;
	}
	
	/**
	Avec l'option blksize, le serveur va commencer par nous confirmer l'utilisation des options,
	Il faut donc vérifier que le opcode reçu soit bien un OACK
	Il faut ensuite envoyé un ACK pour dire au serveur de commencer à nous envoyer les données

	**/


	addrSize = sizeof(struct sockaddr);
	recvSize = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, &sock_in_addr, &addrSize);
	if (recvSize == -1) {
		printf("error while recvfrom: %d\n", recvSize);
		fclose(fileOut);
		return;
	}
	opcode = buffer[1];
	if(opcode == OPCODE_ERROR) {
		printf("error opcode %s\n", &buffer[4]);
		fclose(fileOut);
		return;
	} else if(opcode == OPCODE_OACK) {
		buffer[0] = 0;
		buffer[1] = OPCODE_ACK;
		buffer[2] = 0;
		buffer[3] = 0;
		sendSize = sendto(sockfd, buffer, requestLength, 0, &sock_in_addr, sizeof(struct sockaddr));
	}


	/**
	A partir d'ici on va reçevoir les données par tranche de BLOCK_SIZE octets
	pour chaque packet reçu il faut confirmer la réception avec l'envoie d'un ACK avec l'id du bloc reçus

	**/

	while(1) {

		addrSize = sizeof(struct sockaddr);
		recvSize = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, &sock_in_addr, &addrSize);
		if (recvSize == -1) {
			printf("error while recvfrom: %d\n", recvSize);
		}
		opcode = buffer[1];//L'opcode est sur 2 octet, mais comme il est au maximum de 6, nous n'avons besoin de regarder que l'octet de poid faible
		if(opcode == OPCODE_ERROR) {
			printf("error opcode %s\n", &buffer[4]);
			return;
		}

		//Un décalage est un cast permettent de mettre en forme les données reçus pour avoir le bloc id
		blockId = (unsigned char)buffer[2]<<8 | (unsigned char)buffer[3];

		requestLength = 4;//On passe la taille en 4 (opcode + block id)

		buffer[0] = 0;
		buffer[1] = OPCODE_ACK;
		sendSize = sendto(sockfd, buffer, requestLength, 0, &sock_in_addr, sizeof(struct sockaddr));

		printf("received block id %d : %d bytes\n", blockId, recvSize-4);
		if ((recvSize - 4 > 0)  &&  (blockId > blockCounter)) {
			fwrite(&buffer[4], 1, recvSize - 4, fileOut);
			blockCounter++;
		}
	
		//Pour savoir si c'est le dernier packet, il faut regarder si le nombre d'octet reçus est inférieur au nombre max.
		//Le -4 est pour ne pas prendre en compte les 2 octets de l'op code et les 2 octets du bloc id

		if(recvSize - 4 < BLOCK_SIZE) {
			fclose(fileOut);
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
		printf("Usage: getftp <host> <local file>\n");
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
	
	getFile(args[2], res->ai_addr, sockfd);

	close(sockfd);
	freeaddrinfo(res);
}
