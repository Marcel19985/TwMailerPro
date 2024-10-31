#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024
#define PORT 6543

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
   int create_socket; //Rückgabewert für socket creation
   char buffer[BUF]; //Nachrichten (in und out) speichern
   struct sockaddr_in address; //Serveradressen- Infos
   int size; //Nachrichtengröße
   int isQuit; //wie bool, um zu speichern ob client das Programm schließen will

   ////////////////////////////////////////////////////////////////////////////
   // CREATE A SOCKET
   // https://man7.org/linux/man-pages/man2/socket.2.html
   // https://man7.org/linux/man-pages/man7/ip.7.html
   // https://man7.org/linux/man-pages/man7/tcp.7.html
   // IPv4, TCP (connection oriented), IP (same as server)
   if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   { //wenn Rückgabewert -1 -> Fehler
      perror("Socket error");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // INIT ADDRESS
   // Attention: network byte order => big endian
   memset(&address, 0, sizeof(address)); // init storage with 0
   address.sin_family = AF_INET;         // IPv4
   // https://man7.org/linux/man-pages/man3/htons.3.html
   address.sin_port = htons(PORT);
   // https://man7.org/linux/man-pages/man3/inet_aton.3.html
   if (argc < 2)
   { //wenn keine IP Adresse übergeben, wird auf localhost (127.0.0.1) gesetzt
      inet_aton("127.0.0.1", &address.sin_addr); //IP String nach binär umwandeln
   }
   else
   {
      inet_aton(argv[1], &address.sin_addr); //gegebene IP Adresse nach binär umwandeln
   }

   ////////////////////////////////////////////////////////////////////////////
   // CREATE A CONNECTION
   // https://man7.org/linux/man-pages/man2/connect.2.html
   if (connect(create_socket, //Verbindung zu Server aufbauen
               (struct sockaddr *)&address,
               sizeof(address)) == -1)
   { //Wenn Rückgabewert -1 -> Fehler
      // https://man7.org/linux/man-pages/man3/perror.3.html
      perror("Connect error - no server available");
      return EXIT_FAILURE;
   }

   // ignore return value of printf
   //Success message + IP Adresse vom Server
   printf("Connection with server (%s) established\n",
          inet_ntoa(address.sin_addr));

   ////////////////////////////////////////////////////////////////////////////
   // RECEIVE DATA
   // https://man7.org/linux/man-pages/man2/recv.2.html
   size = recv(create_socket, buffer, BUF - 1, 0); //Rückmeldung vom Server wird im Puffer gespeichert
   if (size == -1)
   {
      perror("recv error");
   }
   else if (size == 0)
   {
      printf("Server closed remote socket\n"); // ignore error
   }
   else
   {
      buffer[size] = '\0';
      printf("%s", buffer); //Ausgabe von der Rückmeldung
   }

   do
   { //Loop wird solange ausgeführt bis user "quit" eingibt
      printf(">> ");
      if (fgets(buffer, BUF - 1, stdin) != NULL) //Input vom User
      {
         int size = strlen(buffer);
         // remove new-line signs from string at the end
         if (buffer[size - 2] == '\r' && buffer[size - 1] == '\n')
         {
            size -= 2;
            buffer[size] = 0;
         }
         else if (buffer[size - 1] == '\n')
         {
            --size;
            buffer[size] = 0;
         }
         isQuit = strcmp(buffer, "quit") == 0; //falls User quit eingegeben hat

         //////////////////////////////////////////////////////////////////////
         // SEND DATA
         // https://man7.org/linux/man-pages/man2/send.2.html
         // send will fail if connection is closed, but does not set
         // the error of send, but still the count of bytes sent
         if ((send(create_socket, buffer, size + 1, 0)) == -1) 
         {
            // in case the server is gone offline we will still not enter
            // this part of code: see docs: https://linux.die.net/man/3/send
            // >> Successful completion of a call to send() does not guarantee 
            // >> delivery of the message. A return value of -1 indicates only 
            // >> locally-detected errors.
            // ... but
            // to check the connection before send is sense-less because
            // after checking the communication can fail (so we would need
            // to have 1 atomic operation to check...)
            perror("send error");
            break; //bei Fehler wird Loop unterbrochen
         }

         //////////////////////////////////////////////////////////////////////
         // RECEIVE FEEDBACK
         // consider: reconnect handling might be appropriate in somes cases
         //           How can we determine that the command sent was received 
         //           or not? 
         //           - Resend, might change state too often. 
         //           - Else a command might have been lost.
         //
         // solution 1: adding meta-data (unique command id) and check on the
         //             server if already processed.
         // solution 2: add an infrastructure component for messaging (broker)
         //
         size = recv(create_socket, buffer, BUF - 1, 0);
         if (size == -1)
         {
            perror("recv error");
            break; //bei Fehler wird Loop unterbrochen
         }
         else if (size == 0)
         {
            printf("Server closed remote socket\n"); // ignore error
            break;
         }
         else
         {
            buffer[size] = '\0';
            printf("<< %s\n", buffer); // ignore error
            if (strcmp("OK", buffer) != 0)
            {
               fprintf(stderr, "<< Server error occured, abort\n");
               break;
            }
         }
      }
   } while (!isQuit);

   ////////////////////////////////////////////////////////////////////////////
   // CLOSES THE DESCRIPTOR
   if (create_socket != -1)
   {
      if (shutdown(create_socket, SHUT_RDWR) == -1)
      {
         // invalid in case the server is gone already
         perror("shutdown create_socket"); 
      }
      if (close(create_socket) == -1)
      {
         perror("close create_socket");
      }
      create_socket = -1;
   }

   return EXIT_SUCCESS;
}
