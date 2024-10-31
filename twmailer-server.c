#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h> //Für sockaddr_in Struktur und IP-Adressen
#include <arpa/inet.h>
#include <unistd.h> //Für Funktionen wie close() und read()
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h> //Für Verzeichnisfunktionen wie opendir()
#include <errno.h>
#include <sys/stat.h> //Für mkdir() Funktion
#include <time.h> //Für die Zeitfunktion time()
#include <signal.h>
#include <pthread.h> //Für Threading und Mutex

#define BUF 1024
#define PATH_BUF 2048 //größerer Buffer für Dateipfade

char mail_spool_directory[BUF]; // Verzeichnis wo Email gespeichert
int abortRequested = 0; //Flag für Abbruch
int create_socket = -1; //Socket für Server
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex für Dateizugriff
pthread_mutex_t abort_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex für abortRequested

void signalHandler(int sig); //Signalbehandlung
void *clientCommunication(void *data); //Kommunikation mit Client
void handle_send(int socket, char *buffer); //SEND
void handle_list(int socket, char *buffer); //LIST
void handle_read(int socket, char *buffer); //READ
void handle_del(int socket, char *buffer); //DEL

int main(int argc, char **argv)
{
    struct sockaddr_in address, cliaddress; //Strukturen für Server- und Client-Adressen
    socklen_t addrlen; //Länge der Adresse
    int reuseValue = 1; //Option für Wiederverwenden von Adressen

    if (argc < 3) { //Mind. 3 Argumente: Programmname, Port, Mail-Spool-Verzeichnis
        fprintf(stderr, "Usage: %s <port> <mail-spool-directory>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]); //Portnummer aus Argument holen
    strcpy(mail_spool_directory, argv[2]); //Mail-Spool-Verzeichnis übernehmen

    //Verzeichnis öffnen oder erstellen falls es nicht existiert:
    DIR *dir = opendir(mail_spool_directory);
    if (!dir) {
        if (mkdir(mail_spool_directory, 0777) == -1) {
            perror("Failed to create mail spool directory");
            return EXIT_FAILURE;
        }
    } else {
        closedir(dir);
    }

    // Signal handler für SIGINT:
    if (signal(SIGINT, signalHandler) == SIG_ERR) {
        perror("Signal cannot be registered");
        return EXIT_FAILURE;
    }

    //Socket erstellen
    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket error");
        return EXIT_FAILURE;
    }

    //Setzen der Socket-Optionen, um Adresse und Port wiederzuverwenden:
    if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEADDR, &reuseValue, sizeof(reuseValue)) == -1) {
        perror("Set socket options - reuseAddr");
        return EXIT_FAILURE;
    }

    //Initialisieren der Serveradresse:
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET; //Adressfamilie (IPv4)
    address.sin_addr.s_addr = INADDR_ANY; //Akzeptiert Verbindungen von jeder IP
    address.sin_port = htons(port); //Portnummer

    // Binden der Adresse an Socket:
    if (bind(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("Bind error");
        return EXIT_FAILURE;
    }

    //Warten auf eingehende Verbindungen (max. 5 gleichzeitig):
    if (listen(create_socket, 5) == -1) {
        perror("Listen error");
        return EXIT_FAILURE;
    }

    while (!abortRequested) {
        int *new_socket = malloc(sizeof(int)); //Speicher für neuen Socket allokieren
        addrlen = sizeof(struct sockaddr_in);
        *new_socket = accept(create_socket, (struct sockaddr *)&cliaddress, &addrlen);

        if (*new_socket == -1) {
            if (abortRequested) {
                perror("Accept error after aborted");
            } else {
                perror("Accept error");
            }
            free(new_socket);
            break;
        }
        printf("Client connected from %s:%d...\n", inet_ntoa(cliaddress.sin_addr), ntohs(cliaddress.sin_port));
        
        pthread_t thread;
        pthread_create(&thread, NULL, clientCommunication, new_socket); //Thread erstellen
        pthread_detach(thread); //Thread als detached markieren
    }

    //Socket schließen, wenn das Programm beendet wird:
    if (create_socket != -1) {
        if (shutdown(create_socket, SHUT_RDWR) == -1) {
            perror("Shutdown server socket");
        }
        if (close(create_socket) == -1) {
            perror("Close server socket");
        }
        create_socket = -1;
    }

    pthread_mutex_destroy(&file_mutex); //Mutex zerstören
    pthread_mutex_destroy(&abort_mutex); //Mutex zerstören
    return EXIT_SUCCESS;
}

void signalHandler(int sig)
{
    if (sig == SIGINT) {
        printf("Abort requested...\n");
        pthread_mutex_lock(&abort_mutex); // Mutex für Zugriff auf abortRequested
        abortRequested = 1; //Abbruchanforderung setzen
        pthread_mutex_unlock(&abort_mutex);

        if (create_socket != -1) { //Schließe Server-Socket
            if (shutdown(create_socket, SHUT_RDWR) == -1) {
                perror("Shutdown create_socket");
            }
            if (close(create_socket) == -1) {
                perror("Close create_socket");
            }
            create_socket = -1;
        }
    } else {
        exit(sig); //bei anderen Signalen beenden
    }
}

void *clientCommunication(void *data) //Kommunikation mit Client:
{
int client_socket = *((int *)data); //Socket-Deskriptor
    free(data); //Speicher für den Socket freigeben

    char buffer[BUF]; //Buffer für empfangene Nachrichten
    int size;

    //Nachrichten verarbeiten:
    while ((size = recv(client_socket, buffer, BUF - 1, 0)) > 0) {
        buffer[size] = '\0'; //Puffer null terminieren
        if (strncmp(buffer, "SEND", 4) == 0) {
            pthread_mutex_lock(&file_mutex); // Lock mutex for file operations
            handle_send(client_socket, buffer); // Process SEND
            pthread_mutex_unlock(&file_mutex); // Unlock mutex
        } else if (strncmp(buffer, "LIST", 4) == 0) {
            pthread_mutex_lock(&file_mutex); // Lock mutex for file operations
            handle_list(client_socket, buffer); // Process LIST
            pthread_mutex_unlock(&file_mutex); // Unlock mutex
        } else if (strncmp(buffer, "READ", 4) == 0) {
            pthread_mutex_lock(&file_mutex); // Lock mutex for file operations
            handle_read(client_socket, buffer); // Process READ
            pthread_mutex_unlock(&file_mutex); // Unlock mutex
        } else if (strncmp(buffer, "DEL", 3) == 0) {
            pthread_mutex_lock(&file_mutex); // Lock mutex for file operations
            handle_del(client_socket, buffer); // Process DEL
            pthread_mutex_unlock(&file_mutex); // Unlock mutex
        } else if (strncmp(buffer, "QUIT", 4) == 0) {
            break; // Exit loop if QUIT command is received
        }
    }

    close(client_socket); // Close the client socket when done
    return NULL;
}

void handle_send(int socket, char *buffer) {

    char sender[9], receiver[9], subject[81], message[4096], filepath[PATH_BUF];
    FILE *file;
    
    //Parst Sender, Empfänger und Betreff aus dem Puffer:
    if (sscanf(buffer, "SEND\n%8s\n%8s\n%80[^\n]\n%[^\n]", sender, receiver, subject, message) != 4) {
        fprintf(stderr, "Error: Invalid SEND format\n");
        send(socket, "ERR\n", 4, 0); //Fehler senden
        return;
    }

    //Erstellt das Verzeichnis des Empfängers, falls es nicht existiert:
    snprintf(filepath, sizeof(filepath), "%s/%s", mail_spool_directory, receiver);
    
    DIR *dir = opendir(filepath);
    if (!dir) {
        if (mkdir(filepath, 0777) == -1) {
            perror("Failed to create inbox directory");
            send(socket, "ERR\n", 4, 0);
            return;
        }
    } else {
        closedir(dir);
    }

    //Erstelle eine neue Nachrichtendatei mit Zeitstempel:
    snprintf(filepath, sizeof(filepath), "%s/%s/message_%ld.txt", mail_spool_directory, receiver, (long)time(NULL));
    
    file = fopen(filepath, "w");
    if (!file) {
        perror("Failed to create message file");
        send(socket, "ERR\n", 4, 0);
        return;
    }

    // Schreibe Nachricht in die Datei:
    fprintf(file, "Sender: %s\nReceiver: %s\nSubject: %s\nMessage:\n%s\n", sender, receiver, subject, message);
    fclose(file);

    send(socket, "OK\n", 3, 0); //Erfolgsnachricht senden
}

void handle_list(int socket, char *buffer) {
    char username[9], filepath[PATH_BUF], response[BUF];
    struct dirent *entry;
    DIR *dir;
    int count = 0;

    sscanf(buffer, "LIST\n%8s", username); //Benutzername parsen

    //Path zum Mailverzeichnis des Users erstellen:
    int snprintf_result = snprintf(filepath, sizeof(filepath), "%s/%s", mail_spool_directory, username);
    if (snprintf_result >= sizeof(filepath) || snprintf_result < 0) {
        send(socket, "ERR\n", 4, 0);
        return;
    }

    //Verzeichnis öffnen:
    if ((dir = opendir(filepath)) == NULL) {
        send(socket, "0\n", 2, 0);  //Kein Benutzerordner vorhanden
        return;
    }

    //Durchsuche das Verzeichnis nach Nachrichten
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, "message_") != NULL) {
            count++;  //Nachrichtencounter erhöhen
            FILE *file;
            char message_path[PATH_BUF], line[BUF], subject[81];

            //Gesamtes Verzeichnis bis Nachrichtenpfad erstellen:
            snprintf_result = snprintf(message_path, sizeof(message_path), "%s/%s", filepath, entry->d_name);
            if (snprintf_result >= sizeof(message_path) || snprintf_result < 0) {
                send(socket, "ERR\n", 4, 0);
                closedir(dir);
                return;
            }

            //File öffnen und Betreff (subject) Line extrahieren:
            file = fopen(message_path, "r");
            if (file) {
                //Sender und Empfänger überspringen:
                fgets(line, sizeof(line), file);  //Skip sender
                fgets(line, sizeof(line), file);  //Skip receiver
                
                //Dritte Zeile auslesen (Betreff):
                fgets(subject, sizeof(subject), file);
                subject[strcspn(subject, "\n")] = 0;  //Newline \n entfernen
                fclose(file);

                //Betreff zum Client senden:
                snprintf_result = snprintf(response, sizeof(response), "%s\n", subject);
                if (snprintf_result >= sizeof(response) || snprintf_result < 0) {
                    send(socket, "ERR\n", 4, 0);
                    closedir(dir);
                    return;
                }
                send(socket, response, strlen(response), 0);  //Betreff zum Client senden
            }
        }
    }

    //Anzahl an Emails ausgeben:
    snprintf_result = snprintf(response, sizeof(response), "%d\n", count);
    if (snprintf_result >= sizeof(response) || snprintf_result < 0) {
        send(socket, "ERR\n", 4, 0);
        closedir(dir);
        return;
    }
    send(socket, response, strlen(response), 0);  //Count zum Client schicken
    closedir(dir);

}

void handle_read(int socket, char *buffer) {

    char username[9], filepath[PATH_BUF];
    int message_num, count = 0;
    struct dirent *entry;
    DIR *dir;

    sscanf(buffer, "READ\n%8s\n%d", username, &message_num); //Benutzername und Nachrichtennummer parsen
    int snprintf_result = snprintf(filepath, sizeof(filepath), "%s/%s", mail_spool_directory, username);
    if (snprintf_result >= sizeof(filepath) || snprintf_result < 0) {
        send(socket, "ERR\n", 4, 0);
        return;
    }

    if ((dir = opendir(filepath)) == NULL) { //Fehler senden, wenn das Verzeichnis nicht existiert
        send(socket, "ERR\n", 4, 0);
        return;
    }

    while ((entry = readdir(dir)) != NULL) { //Durchsuche Verzeichnis nach gewünschter Nachricht
        if (strstr(entry->d_name, "message_") != NULL) {
            count++;
            if (count == message_num) {
                FILE *file;
                char message_path[PATH_BUF], message[BUF];
                snprintf_result = snprintf(message_path, sizeof(message_path), "%s/%s", filepath, entry->d_name);
                if (snprintf_result >= sizeof(message_path) || snprintf_result < 0) {
                    send(socket, "ERR\n", 4, 0);
                    closedir(dir);
                    return;
                }

                file = fopen(message_path, "r");
                if (!file) {
                    send(socket, "ERR\n", 4, 0);
                    closedir(dir);
                    return;
                }

                //Nachricht Zeile für Zeile senden:
                while (fgets(message, sizeof(message), file)) {
                    send(socket, message, strlen(message), 0);
                }

                fclose(file);
                closedir(dir);
                send(socket, "OK\n", 3, 0); //Erfolgsnachricht senden
                return;
            }
        }
    }

    send(socket, "ERR\n", 4, 0);
    closedir(dir);

}

void handle_del(int socket, char *buffer) {

    char username[9], filepath[PATH_BUF];
    int message_num, count = 0;
    struct dirent *entry;
    DIR *dir;

    sscanf(buffer, "DEL\n%8s\n%d", username, &message_num); //Benutzername und Nachrichtennummer parsen
    int snprintf_result = snprintf(filepath, sizeof(filepath), "%s/%s", mail_spool_directory, username);
    if (snprintf_result >= sizeof(filepath) || snprintf_result < 0) {
        send(socket, "ERR\n", 4, 0); //Fehler senden, wenn das Verzeichnis nicht existiert
        return;
    }

    if ((dir = opendir(filepath)) == NULL) {
        send(socket, "ERR\n", 4, 0);
        return;
    }

    //Durchsuche das Verzeichnis nach der zu löschenden Nachricht:
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, "message_") != NULL) {
            count++;
            if (count == message_num) {
                char message_path[PATH_BUF];
                snprintf_result = snprintf(message_path, sizeof(message_path), "%s/%s", filepath, entry->d_name);
                if (snprintf_result >= sizeof(message_path) || snprintf_result < 0) {
                    send(socket, "ERR\n", 4, 0);
                    closedir(dir);
                    return;
                }

                if (remove(message_path) == 0) {
                    send(socket, "OK\n", 3, 0); //Erfolgsnachricht senden
                } else {
                    send(socket, "ERR\n", 4, 0); //Fehler senden
                }
                closedir(dir);
                return;
            }
        }
    }

    send(socket, "ERR\n", 4, 0); //Fehler, wenn die Nachricht nicht gefunden wurde
    closedir(dir);

}
