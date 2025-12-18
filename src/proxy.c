#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <stdbool.h>
#include "simpleSocketAPI.h"

#define SERVADDR "127.0.0.1" // Adresse d'écoute du proxy
#define SERVPORT "0"         // Port choisi dynamiquement
#define LISTENLEN 1          // Taille de la file des demandes de connexion
#define MAXBUFFERLEN 1024    // Taille du tampon pour les échanges de données
#define MAXHOSTLEN 64        // Taille d'un nom de machine
#define MAXPORTLEN 64        // Taille d'un numéro de port
#define FTPPORT "21"         // Port du serveur FTP

int main(int argc, char *argv[])
{
    int ecode;                       // Codes retour
    char serverAddr[MAXHOSTLEN];     // Adresse locale du proxy
    char serverPort[MAXPORTLEN];     // Port local du proxy
    char login[50];                  // Login utilisateur (en argument)
    char realServerAddr[MAXHOSTLEN]; // Adresse du vrai serveur FTP (en argument)
    int descSockRDV;                 // Socket de rendez-vous (proxy côté client)
    int descSockCOM;                 // Socket de communication avec le client FTP
    struct addrinfo hints;           // Contrôle getaddrinfo
    struct addrinfo *res;            // Résultat getaddrinfo
    struct sockaddr_storage myinfo;  // Infos sur la socket locale
    struct sockaddr_storage from;    // Infos sur le client
    socklen_t len;                   // Longueur des structures de socket
    char buffer[MAXBUFFERLEN];       // Tampon de communication
    int sockCTL_PS;                  // Socket de contrôle vers le vrai serveur FTP

    // Vérification des arguments
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <login> <serveur_ftp>\n", argv[0]);
        fprintf(stderr, "Exemple: %s bob localhost\n", argv[0]);
        exit(1);
    }

    strncpy(login, argv[1], sizeof(login) - 1);
    login[sizeof(login) - 1] = '\0';
    strncpy(realServerAddr, argv[2], sizeof(realServerAddr) - 1);
    realServerAddr[sizeof(realServerAddr) - 1] = '\0';

    printf("Proxy pour utilisateur: %s -> serveur: %s\n", login, realServerAddr);

    // Création de la socket de RDV
    descSockRDV = socket(AF_INET, SOCK_STREAM, 0);
    if (descSockRDV == -1)
    {
        perror("Erreur création socket RDV");
        exit(2);
    }

    // Préparation de hints pour bind()
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;

    // Récupération des infos d'adresse pour le proxy
    ecode = getaddrinfo(SERVADDR, SERVPORT, &hints, &res);
    if (ecode)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ecode));
        close(descSockRDV);
        exit(1);
    }

    // Liaison de la socket
    ecode = bind(descSockRDV, res->ai_addr, res->ai_addrlen);
    if (ecode == -1)
    {
        perror("Erreur liaison socket RDV");
        freeaddrinfo(res);
        close(descSockRDV);
        exit(3);
    }
    freeaddrinfo(res);

    // Récupération adresse/port utilisés
    len = sizeof(struct sockaddr_storage);
    ecode = getsockname(descSockRDV, (struct sockaddr *)&myinfo, &len);
    if (ecode == -1)
    {
        perror("getsockname");
        close(descSockRDV);
        exit(4);
    }

    ecode = getnameinfo((struct sockaddr *)&myinfo, sizeof(myinfo),
                        serverAddr, MAXHOSTLEN, serverPort, MAXPORTLEN,
                        NI_NUMERICHOST | NI_NUMERICSERV);
    if (ecode != 0)
    {
        fprintf(stderr, "getnameinfo: %s\n", gai_strerror(ecode));
        close(descSockRDV);
        exit(4);
    }

    printf("Proxy FTP à l'écoute sur %s:%s\n", serverAddr, serverPort);

    // Listen
    ecode = listen(descSockRDV, LISTENLEN);
    if (ecode == -1)
    {
        perror("listen");
        close(descSockRDV);
        exit(5);
    }

    // Accept client
    len = sizeof(struct sockaddr_storage);
    descSockCOM = accept(descSockRDV, (struct sockaddr *)&from, &len);
    if (descSockCOM == -1)
    {
        perror("accept");
        close(descSockRDV);
        exit(6);
    }
    printf("Client connecté\n");

    // Bannière proxy au client
    snprintf(buffer, sizeof(buffer), "220 Proxy FTP pour %s @ %s\r\n", login, realServerAddr);
    write(descSockCOM, buffer, strlen(buffer));

    // Connexion au vrai serveur FTP
    ecode = connect2Server(realServerAddr, FTPPORT, &sockCTL_PS);
    if (ecode == -1)
    {
        perror("connect2Server");
        close(descSockCOM);
        close(descSockRDV);
        exit(11);
    }
    printf("Connecté au serveur FTP %s:21\n", realServerAddr);

    // Lecture bannière serveur FTP -> client
    ecode = read(sockCTL_PS, buffer, sizeof(buffer) - 1);
    if (ecode > 0)
    {
        buffer[ecode] = '\0';
        printf("Bannière serveur: %s", buffer);
        write(descSockCOM, buffer, ecode);
    }

    printf("Relavage activé (Ctrl+C pour arrêter)\n");
    fflush(stdout);

    // BOUCLE DE RELAYAGE
    while (1)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(descSockCOM, &readfds);
        FD_SET(sockCTL_PS, &readfds);
        int maxfd = (descSockCOM > sockCTL_PS ? descSockCOM : sockCTL_PS) + 1;

        ecode = select(maxfd, &readfds, NULL, NULL, NULL);
        if (ecode == -1)
        {
            perror("select");
            break;
        }

        // Client -> Serveur FTP
        if (FD_ISSET(descSockCOM, &readfds))
        {
            ecode = read(descSockCOM, buffer, sizeof(buffer) - 1);
            if (ecode <= 0)
            {
                printf("Client déconnecté\n");
                break;
            }
            buffer[ecode] = '\0';
            printf("C->S: %s", buffer);
            write(sockCTL_PS, buffer, strlen(buffer));
        }

        // Serveur FTP -> Client
        if (FD_ISSET(sockCTL_PS, &readfds))
        {
            ecode = read(sockCTL_PS, buffer, sizeof(buffer) - 1);
            if (ecode <= 0)
            {
                printf("Serveur déconnecté\n");
                break;
            }
            buffer[ecode] = '\0';
            printf("S->C: %s", buffer);
            write(descSockCOM, buffer, strlen(buffer));
        }
    }

    close(sockCTL_PS);
    close(descSockCOM);
    close(descSockRDV);
    return 0;
}
