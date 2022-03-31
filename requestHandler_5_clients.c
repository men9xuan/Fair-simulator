#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <semaphore.h>
#include <arpa/inet.h>

// #include "fair.h"

// Handle client requests coming in through the server socket.  This code should run
// indefinitiely.  It should wait for a client to send a request, process it, and then
// close the client connection and wait for another client.  The requests that may be
// handled are as follows:
//
//   SHUTDOWN - causes the fair server to go offline.  No response is returned.
//
//   ADMIT - contains guest's process ID as well. return a list of all rides and their
//			 ticketRequirements.
//
//	 GET_WAIT_ESTIMATE - takes a ride ID as well.   It then returns an estimate as to
//						 how long of a wait (in seconds) the guest would have to wait
//						 in order to get on the ride.
//
//	 GET_IN_LINE - takes a ride ID and guest's process ID as well.  It then causes the
//				   guest to get in line for the specified ride ... assuming that the
//				   ride ID was valid and that the line hasn't reached its maximum.
//				   An OK response should be returned if all went well, otherwise NO.
//
//   LEAVE_FAIR - takes a guest's process ID.  It then causes the guest to leave the fair.
//				  No response is returned.
sem_t mutex;
volatile int rideId = -1;
volatile int pid;

void *handleIncomingRequests(void *x)
{
  Fair *oFair = (Fair *)x;

  // init semaphore
  if (sem_init(&mutex, 0, 1) < 0)
  {
    printf("Error: on semaphore init.\n");
    exit(1);
  }

  int serverSocket, clientSocket;
  struct sockaddr_in serverAddress, clientAddress;
  int status, addrSize, bytesRcv;
  char inStr[80];
  char buffer[80];
  char outStr[200];

  // Create the client socket
  serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (serverSocket < 0)
  {
    printf("*** SERVER ERROR: Could not open socket.\n");
    exit(-1);
  }

  // Setup the server address
  memset(&serverAddress, 0, sizeof(serverAddress)); // zeros the struct
  serverAddress.sin_family = AF_INET;
  serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
  serverAddress.sin_port = htons((unsigned short)SERVER_PORT);

  // Bind the server socket
  status = bind(serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
  if (status < 0)
  {
    printf("*** SERVER ERROR: Could not bind socket.\n");
    exit(-1);
  }

  // Set up the line-up to handle up to 5 clients in line
  status = listen(serverSocket, 5); //??? 5
  if (status < 0)
  {
    printf("*** SERVER ERROR: Could not listen on socket.\n");
    exit(-1);
  }

  addrSize = sizeof(clientAddress);
  // Wait for clients now
  printf("Fair server online\n");
  int run = 1;
  // volatile int rideId = -1;
  // volatile int pid;
  while (run)
  {
    // Go into infinite loop to talk to client
    clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddress, &addrSize);
    if (clientSocket < 0)
    {
      printf("*** SERVER ERROR: Could not accept incoming client connection.\n");
      exit(-1);
    }
    bytesRcv = recv(clientSocket, buffer, sizeof(buffer), 0);
    buffer[bytesRcv] = 0;

    switch (buffer[0] - '0')
    {
    case SHUTDOWN: // gracefully?
      run = 0;
      break;
    case ADMIT:
      if (((Fair *)x)->numGuests < MAX_GUESTS)
      {
        pid = atoi(buffer + 1);
        oFair->guestIDs[oFair->numGuests] = pid;
        oFair->numGuests++;

        // send list of ride names and tickets
        // clear return message
        memset(outStr, 0, sizeof(outStr));
        for (int i = 0; i < NUM_RIDES; i++)
        {
          sprintf(outStr + strlen(outStr), "%s", oFair->rides[i].name);
          sprintf(outStr + strlen(outStr), " %d ", oFair->rides[i].ticketsRequired);
        }
        send(clientSocket, outStr, strlen(outStr), 0);
      }
      else
      {
        // not admitted park full
        char *response = "0";
        send(clientSocket, response, strlen(response), 0);
      }
      break;
    case GET_WAIT_ESTIMATE:
      if (sem_wait(&mutex) < 0)
      {
        printf("Error: on semaphore wait.\n");
        exit(1);
      }

      rideId = atoi(buffer + 1);

      if (sem_post(&mutex) < 0)
      {
        printf("Error: on semaphore post.\n");
        exit(1);
      }
      // printf("ride Id: %d", rideId);
      int waitTime = (oFair->rides[rideId].lineupSize) / (oFair->rides[rideId].capacity) *
                     ((oFair->rides[rideId].onOffTime) + (oFair->rides[rideId].waitTime) + (oFair->rides[rideId].rideTime));
      // printf("lineupSize: %d\n", oFair->rides[rideId].lineupSize);
      // printf("  Wait time: %d\n", waitTime);
      memset(outStr, 0, sizeof(outStr));
      sprintf(outStr + strlen(outStr), "%d", waitTime);
      send(clientSocket, outStr, strlen(outStr), 0);
      break;

    case GET_IN_LINE:
      pid = atoi(buffer + 1);
      if (rideId >= 0 && rideId < NUM_RIDES && (oFair->rides[rideId].lineupSize) < MAX_LINEUP)
      {
        // add guest to waitingLine
        oFair->rides[rideId].waitingLine[oFair->rides[rideId].lineupSize] = pid;
        oFair->rides[rideId].lineupSize++;
        memset(outStr, 0, sizeof(outStr));
        sprintf(outStr + strlen(outStr), "%d", 1);
        send(clientSocket, outStr, strlen(outStr), 0);
      }
      else
      {
        memset(outStr, 0, sizeof(outStr));
        sprintf(outStr + strlen(outStr), "%d", 0);
        send(clientSocket, outStr, strlen(outStr), 0);
      }
      break;
    case LEAVE_FAIR:
      // printf("Leave Fair\n");
      pid = atoi(buffer + 1);
      for (int i = 0; i < (oFair->numGuests); i++)
      {
        if (oFair->guestIDs[i] == pid)
        {
          for (int j = i; j < (oFair->numGuests); j++)
          {
            // shift gustId Array
            oFair->guestIDs[j] = oFair->guestIDs[j + 1];
          }
          // decrement guest num
          oFair->numGuests--;
        }
      }
      break;
    }
    close(clientSocket);
  }
  // printf("SERVER: Closing server socket.\n");
  // close the sockets
  close(serverSocket);
  printf("SERVER: Shutting down.\n");
}