#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/select.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include "./simpleSocketAPI.h"

#define SERVADDR "127.0.0.1"
#define SERVPORT "0"
#define LISTENLEN 1
#define MAXBUFFERLEN 1024
#define MAXHOSTLEN 64
#define MAXPORTLEN 64

int main(int argc, char *argv[])
{
    int ecode;
    char serverAddr[MAXHOSTLEN];
    char serverPort[MAXPORTLEN];
    int descSockRDV, descSockCOM, sockFTP;
    struct addrinfo hints, *res;
    struct sockaddr_storage myinfo, from;
    socklen_t len;
    char buffer[MAXBUFFERLEN];

    // *** VARIABLES CANAL DONNÉES ***
    bool pasvMode = false;
    char pasvIP[16];
    int pasvPort = 0;
    int sockDataToServer = -1;

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <login> <ftp_server>\n", argv[0]);
        exit(1);
    }
    const char *login = argv[1];
    const char *ftpServer = argv[2];

    // Socket RDV (identique)
    descSockRDV = socket(AF_INET, SOCK_STREAM, 0);
    if (descSockRDV == -1)
    {
        perror("socket RDV");
        exit(2);
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;
    ecode = getaddrinfo(SERVADDR, SERVPORT, &hints, &res);
    if (ecode)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ecode));
        exit(1);
    }

    ecode = bind(descSockRDV, res->ai_addr, res->ai_addrlen);
    if (ecode == -1)
    {
        perror("bind");
        exit(3);
    }
    freeaddrinfo(res);

    len = sizeof(myinfo);
    getsockname(descSockRDV, (struct sockaddr *)&myinfo, &len);
    getnameinfo((struct sockaddr *)&myinfo, sizeof(myinfo), serverAddr, MAXHOSTLEN,
                serverPort, MAXPORTLEN, NI_NUMERICHOST | NI_NUMERICSERV);
    printf("Proxy FTP à l'écoute sur %s:%s\n", serverAddr, serverPort);

    listen(descSockRDV, LISTENLEN);
    len = sizeof(from);
    descSockCOM = accept(descSockRDV, (struct sockaddr *)&from, &len);
    if (descSockCOM == -1)
    {
        perror("accept");
        exit(6);
    }

    // Connexion serveur FTP
    if (connect2Server(ftpServer, "21", &sockFTP) == -1)
    {
        fprintf(stderr, "Impossible de se connecter à %s\n", ftpServer);
        exit(7);
    }

    // Bannière proxy
    snprintf(buffer, MAXBUFFERLEN, "220 Proxy FTP pour %s @ %s\r\n", login, ftpServer);
    write(descSockCOM, buffer, strlen(buffer));

    // *** BOUCLE PRINCIPALE CANAL CONTRÔLE ***
    fd_set readfds;
    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(descSockCOM, &readfds);
        FD_SET(sockFTP, &readfds);
        int fdmax = (descSockCOM > sockFTP ? descSockCOM : sockFTP) + 1;

        if (select(fdmax, &readfds, NULL, NULL, NULL) == -1)
        {
            perror("select");
            break;
        }

        // Client -> Serveur FTP
        if (FD_ISSET(descSockCOM, &readfds))
        {
            int n = read(descSockCOM, buffer, MAXBUFFERLEN - 1);
            if (n <= 0)
                break;
            buffer[n] = 0;
            printf("C->S: %s", buffer);
            fflush(stdout);
            write(sockFTP, buffer, n);
        }

        // *** SERVEUR FTP -> CLIENT (AVEC GESTION PASV) ***
        if (FD_ISSET(sockFTP, &readfds))
        {
            int n = read(sockFTP, buffer, MAXBUFFERLEN - 1);
            if (n <= 0)
                break;
            buffer[n] = 0;
            printf("S->C: %s", buffer);
            fflush(stdout);
            write(descSockCOM, buffer, n);

            // *** GESTION PASV ***
            if (strstr(buffer, "227 Entering Passive Mode") && !pasvMode)
            {
                int a, b, c, d, p1, p2;
                if (sscanf(buffer, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
                           &a, &b, &c, &d, &p1, &p2) == 6)
                {
                    pasvPort = (p1 << 8) + p2;
                    snprintf(pasvIP, sizeof(pasvIP), "%d.%d.%d.%d", a, b, c, d);
                    printf("*** PASV: %s:%d ***\n", pasvIP, pasvPort);
                    fflush(stdout);

                    // PROXY se connecte au serveur FTP (données)
                    sockDataToServer = socket(AF_INET, SOCK_STREAM, 0);
                    struct sockaddr_in dataAddr;
                    dataAddr.sin_family = AF_INET;
                    dataAddr.sin_port = htons(pasvPort);
                    inet_pton(AF_INET, pasvIP, &dataAddr.sin_addr);

                    if (connect(sockDataToServer, (struct sockaddr *)&dataAddr, sizeof(dataAddr)) == 0)
                    {
                        printf("*** CONNEXION DONNÉES SERVEUR OK ! ***\n");
                        pasvMode = true;
                    }
                    else
                    {
                        printf("*** ÉCHEC connexion données serveur ***\n");
                        close(sockDataToServer);
                        sockDataToServer = -1;
                    }
                }
            }
        }
    }

    // Nettoyage
    if (sockDataToServer != -1)
        close(sockDataToServer);
    close(sockFTP);
    close(descSockCOM);
    close(descSockRDV);
    return 0;
}
