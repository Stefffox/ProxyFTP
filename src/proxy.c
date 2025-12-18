#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "simpleSocketAPI.h"

#define SERVADDR "127.0.0.1" // Adresse d'écoute du proxy
#define SERVPORT "0"         // Port choisi dynamiquement
#define LISTENLEN 1          // Taille de la file des demandes de connexion
#define MAXBUFFERLEN 1024    // Taille du tampon pour les échanges de données
#define MAXHOSTLEN 64        // Taille d'un nom de machine
#define MAXPORTLEN 64        // Taille d'un numéro de port
#define FTPPORT "21"         // Port du serveur FTP

int main(void)
{
    int ecode;                       // Codes retour
    char serverAddr[MAXHOSTLEN];     // Adresse locale du proxy
    char serverPort[MAXPORTLEN];     // Port local du proxy
    int descSockRDV;                 // Socket de rendez-vous (proxy côté client)
    int descSockCOM;                 // Socket de communication avec le client FTP
    struct addrinfo hints;           // Contrôle getaddrinfo
    struct addrinfo *res;            // Résultat getaddrinfo
    struct sockaddr_storage myinfo;  // Infos sur la socket locale
    struct sockaddr_storage from;    // Infos sur le client
    socklen_t len;                   // Longueur des structures de socket
    char buffer[MAXBUFFERLEN];       // Tampon de communication
    char login[50];                  // Stocke le login utilisateur
    char realServerAddr[MAXHOSTLEN]; // Adresse du vrai serveur FTP
    int sockCTL_PS;                  // Socket de contrôle vers le vrai serveur FTP

    // Création de la socket de RDV
    descSockRDV = socket(AF_INET, SOCK_STREAM, 0);
    if (descSockRDV == -1)
    {
        perror("Erreur création socket RDV");
        exit(2);
    }

    // Préparation de hints pour bind()
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;     // mode serveur
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_family = AF_INET;       // IPv4 uniquement

    // Récupération des infos d'adresse pour le proxy
    ecode = getaddrinfo(SERVADDR, SERVPORT, &hints, &res);
    if (ecode)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ecode));
        exit(1);
    }

    // Liaison de la socket au couple (IP, port)
    ecode = bind(descSockRDV, res->ai_addr, res->ai_addrlen);
    if (ecode == -1)
    {
        perror("Erreur liaison de la socket de RDV");
        freeaddrinfo(res);
        close(descSockRDV);
        exit(3);
    }
    freeaddrinfo(res);

    // Récupération de l'adresse et du port effectivement utilisés
    len = sizeof(struct sockaddr_storage);
    ecode = getsockname(descSockRDV, (struct sockaddr *)&myinfo, &len);
    if (ecode == -1)
    {
        perror("SERVEUR: getsockname");
        close(descSockRDV);
        exit(4);
    }

    ecode = getnameinfo((struct sockaddr *)&myinfo, sizeof(myinfo),
                        serverAddr, MAXHOSTLEN,
                        serverPort, MAXPORTLEN,
                        NI_NUMERICHOST | NI_NUMERICSERV);
    if (ecode != 0)
    {
        fprintf(stderr, "error in getnameinfo: %s\n", gai_strerror(ecode));
        close(descSockRDV);
        exit(4);
    }

    printf("Proxy FTP à l'écoute sur l'adresse : %s\n", serverAddr);
    printf("Proxy FTP à l'écoute sur le port    : %s\n", serverPort);

    // Mise en écoute
    ecode = listen(descSockRDV, LISTENLEN);
    if (ecode == -1)
    {
        perror("Erreur initialisation buffer d'écoute");
        close(descSockRDV);
        exit(5);
    }

    // Attente d'un client FTP
    len = sizeof(struct sockaddr_storage);
    descSockCOM = accept(descSockRDV, (struct sockaddr *)&from, &len);
    if (descSockCOM == -1)
    {
        perror("Erreur accept");
        close(descSockRDV);
        exit(6);
    }

    // Envoi de la bannière FTP au client
    strcpy(buffer, "220 BLABLABLA\r\n");
    if (write(descSockCOM, buffer, strlen(buffer)) == -1)
    {
        perror("Erreur écriture bannière");
        close(descSockCOM);
        close(descSockRDV);
        exit(7);
    }

    // Lecture de la commande USER du client
    ecode = read(descSockCOM, buffer, sizeof(buffer) - 1);
    if (ecode == -1)
    {
        perror("Erreur lecture USER");
        close(descSockCOM);
        close(descSockRDV);
        exit(8);
    }
    buffer[ecode] = '\0';
    printf("Reçu du client FTP : %s", buffer);

    /*
     * Hypothèse simple :
     * Le client envoie : "USER <login>@<adresse_serveur>\r\n"
     * Exemple : "USER bob@ftp.example.org\r\n"
     * On extrait donc login et adresse_serveur.
     */

    char userField[100];
    if (sscanf(buffer, "USER %99s", userField) != 1)
    {
        fprintf(stderr, "Format USER invalide\n");
        close(descSockCOM);
        close(descSockRDV);
        exit(9);
    }

    // Séparation login et serveur (login@serveur)
    char *at = strchr(userField, '@');
    if (at == NULL)
    {
        fprintf(stderr, "Format USER attendu: USER login@serveur\r\n");
        close(descSockCOM);
        close(descSockRDV);
        exit(10);
    }

    *at = '\0';
    strncpy(login, userField, sizeof(login));
    login[sizeof(login) - 1] = '\0';
    strncpy(realServerAddr, at + 1, sizeof(realServerAddr));
    realServerAddr[sizeof(realServerAddr) - 1] = '\0';

    printf("login: %s, adresse serveur FTP: %s\n", login, realServerAddr);

    // Connexion au vrai serveur FTP
    ecode = connect2Server(realServerAddr, FTPPORT, &sockCTL_PS);
    if (ecode == -1)
    {
        perror("Impossible de se connecter sur le serveur FTP distant");
        close(descSockCOM);
        close(descSockRDV);
        exit(11);
    }
    printf("Bien connecté au serveur FTP %s:%s\n", realServerAddr, FTPPORT);

    // Lecture de la bannière du serveur FTP réel
    ecode = read(sockCTL_PS, buffer, sizeof(buffer) - 1);
    if (ecode == -1)
    {
        perror("Erreur lecture bannière serveur FTP");
        close(sockCTL_PS);
        close(descSockCOM);
        close(descSockRDV);
        exit(12);
    }
    buffer[ecode] = '\0';
    printf("Bannière du serveur FTP : %s", buffer);

    // Transmission de la bannière réelle au client
    if (write(descSockCOM, buffer, strlen(buffer)) == -1)
    {
        perror("Erreur renvoi bannière au client");
        close(sockCTL_PS);
        close(descSockCOM);
        close(descSockRDV);
        exit(13);
    }

    /*
     * À partir d’ici, à toi de continuer :
     * - lire les commandes du client,
     * - les renvoyer au serveur FTP,
     * - lire les réponses du serveur,
     * - les renvoyer au client.
     * Tu peux faire une boucle read/write simple pour le contrôle.
     */

    // Fermeture des connexions
    close(sockCTL_PS);
    close(descSockCOM);
    close(descSockRDV);

    return 0;
}
