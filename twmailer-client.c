#include <sys/types.h>
#include <sys/socket.h> //Für Sockets
#include <netinet/in.h>
#include <arpa/inet.h> //Für Umwandlung von IP-Adressen
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h> //Für Ein- und Ausgabe z.B. printf() und fgets()
#include <string.h> //Für Funktionen wie strcmp() und strcat()
#include <ctype.h> //Für Funktionen zur Zeichenverarbeitung wie isdigit()

#define BUF 4096 //Buffergröße = 4096 Bytes

int main(int argc, char **argv) {
    int create_socket;
    struct sockaddr_in address; //zum Speichern der Serveradresse
    char buffer[BUF]; //Buffer fürs Speichern von Daten
    int size; //Hilfsvariable für die Größe von empfangenen Daten

    if (argc < 3) { //Mindestens 3 Argumente: Programmname (immer), IP, Port-Nummer
        fprintf(stderr, "Usage: %s <ip> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    //Socket ersetllen:
    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket error");
        return EXIT_FAILURE;
    }

    //Serveradresse initialisieren:
    address.sin_family = AF_INET; //Adressfamilie IPv4
    address.sin_port = htons(atoi(argv[2])); //Portnummer (wird in Netzwerkbyte-Reihenfolge konvertiert)
    if (inet_aton(argv[1], &address.sin_addr) == 0) { //IP-Adresse konvertieren
        fprintf(stderr, "Invalid IP address\n");
        return EXIT_FAILURE;
    }

    //Server Verbindung herstellen:
    if (connect(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("Connect error");
        return EXIT_FAILURE;
    }

    printf("Connected to the server. Available commands: SEND, LIST, READ, DELETE, QUIT\n");

    while (1) {
        printf(">> ");
        fgets(buffer, BUF, stdin); //Eingabe lesen
        buffer[strcspn(buffer, "\n")] = 0; //Zeilenumbruch \n entfernen

        //SEND:
        if (strncmp(buffer, "SEND", 4) == 0) {
            char sender[9], receiver[9], subject[81], message[BUF], temp[BUF]; //char arrays für die Attribute
            //Eingabe Absender:
            printf("Sender (max. 8 digits): ");
            fgets(sender, sizeof(sender), stdin);
            sender[strcspn(sender, "\n")] = 0; //Zeilenumbruch \n entfernen

            //Eingabe Empfänger:
            printf("Receiver (max. 8 digits): ");
            fgets(receiver, sizeof(receiver), stdin);
            receiver[strcspn(receiver, "\n")] = 0; //Zeilenumbruch \n entfernen

            //Eingabe Betreff:
            printf("Subject (max. 81 digits): ");
            fgets(subject, sizeof(subject), stdin);
            subject[strcspn(subject, "\n")] = 0; //Zeilenumbruch \n entfernen

            printf("Message (end with a single dot '.'): \n"); //Info, dass Nachricht mit einem Punkt beendet werden muss
            message[0] = '\0'; //Nachricht-Buffer initialisieren

            while (fgets(temp, sizeof(temp), stdin)) {
                temp[strcspn(temp, "\n")] = 0; //Zeilenumbruch \n entfernen
                if (strcmp(temp, ".") == 0) {
                    break; //Ende der Nachricht wenn "." am Ende
                }
                strcat(message, temp); //Eingabe zum Nachrichten-Buffer hinzufügen
                strcat(message, "\n"); //Zeilenumbruch nach jeder Zeile hinzufügen
            }

            //Alle Infos in finalen Buffer schreiben:
            int snprintf_result = snprintf(buffer, sizeof(buffer), "SEND\n%s\n%s\n%s\n%s\n.\n", sender, receiver, subject, message);
            if (snprintf_result >= sizeof(buffer) || snprintf_result < 0) {
                fprintf(stderr, "Error: Message too long to send\n");
                continue; //Nachricht nicht senden, wenn ein Fehler mit Buffer
            }

            //Nachricht an Server senden:
            send(create_socket, buffer, strlen(buffer), 0);

        } 
        //LIST:
        else if (strncmp(buffer, "LIST", 4) == 0) {
            char username[8];
            printf("Username (max. 8 digits): ");
            fgets(username, sizeof(username), stdin); //Einlesen von Benutzername
            username[strcspn(username, "\n")] = 0; //Zeilenumbruch \n entfernen

            snprintf(buffer, sizeof(buffer), "LIST\n%s\n", username); //LIST-Befehl mit dem Benutzernamen kombinieren
            send(create_socket, buffer, strlen(buffer), 0);

            char response[BUF]; //Serverantwort empfangen
            int total_messages = 0;

            while (1) {
                ssize_t size = recv(create_socket, response, sizeof(response) - 1, 0);
                if (size <= 0) { //Fehler oder Server geschlossen
                    if (size == 0) {
                        printf("Server closed the connection.\n");
                    } else {
                        perror("Recv error");
                    }
                    break;
                }
                
                response[size] = '\0'; //Empfangenes Null-terminieren

                //Überprüfen, ob die empfangene Nachricht Zahl (Anzahl der Nachrichten) ist
                if (isdigit(response[0])) { 
                    printf("Count of messages of the user: %s", response); //Ausgabe der Anzahl der Nachrichten
                    break;
                } else { //Ausgabe der Betreffzeilen
                    printf("%s", response);
                    total_messages++;
                    continue;
                }

            }
            if (total_messages == 0) { //wenn keine Nachricht vorhanden
                printf("No messages found for the user.\n");
                continue;
            }
            continue;
        }

        //READ und DEL:
        else if (strncmp(buffer, "READ", 4) == 0 || strncmp(buffer, "DEL", 3) == 0) {
            char username[9], message_number[BUF];

            //Benutzernamen eingeben:
            printf("Username (max. 8 digits): ");
            fgets(username, sizeof(username), stdin);
            username[strcspn(username, "\n")] = 0; //Zeilenumbruch \n entfernen

            //Nachricht auswählen:
            printf("Message number: ");
            fgets(message_number, sizeof(message_number), stdin);
            message_number[strcspn(message_number, "\n")] = 0; //Zeilenumbruch \n entfernen

            //Befehl mit Benutzername + Nachrichtennummer kombinieren:
            int snprintf_result = snprintf(buffer, sizeof(buffer), "%s\n%s\n%s\n", strncmp(buffer, "READ", 4) == 0 ? "READ" : "DEL", username, message_number);
            if (snprintf_result >= sizeof(buffer) || snprintf_result < 0) {
                fprintf(stderr, "Error: Command too long to send\n");
                continue;
            }

            send(create_socket, buffer, strlen(buffer), 0);

            //Server response:
            size = recv(create_socket, buffer, BUF - 1, 0);
            if (size == -1) {
                perror("Recv error");
            } else {
                buffer[size] = '\0';  //Empfangene Nachricht Null-terminieren
                printf("%s\n", buffer); //Antwort ausgeben
            }
            continue;
        }
        // QUIT:
        else if (strncmp(buffer, "QUIT", 4) == 0) {
            send(create_socket, "QUIT\n", 5, 0); //QUIT-Befehl an den Server senden
            break; //Schleife beenden
        } 
        else { //wenn nicht SEND, LIST, READ, DEL oder QUIT eingegeben wurde:
            printf("Unknown command. Available commands: SEND, LIST, READ, DEL, QUIT\n");
            continue;
        }

            //Serverantwort empfangen und anzeigen:
            size = recv(create_socket, buffer, BUF - 1, 0);
            if (size == -1) {
                perror("Recv error");
            } else {
                buffer[size] = '\0';
                printf("%s\n", buffer);
            }
    }

    //Socket nach QUIT schließen:
    close(create_socket);
    return EXIT_SUCCESS;
}
