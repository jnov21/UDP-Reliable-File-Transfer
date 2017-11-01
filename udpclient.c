/*****************************************
 * Caleb Hayward
 * Justin Novack
 * 2/21/2017
 * CIS457 - Project #2
 * Reliable File Transfer over UDP
 * udpclient.c
 ******************************************/

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACKET_TYPE_SHIFT 0
#define PACKET_ID_SHIFT 4
#define NUM_PACKETS_SHIFT 8
#define DATA_SIZE_SHIFT 12
#define DATA_SHIFT 20
#define PARITY_SHIFT 1020

#define MAX_PACKET_SIZE 1024
#define MAX_DATA_SIZE 1000
#define TYPE_SIZE 4
#define WINDOW_SIZE 5

// Packet structure
typedef struct{
  int recvFlag; // Flag to indicate whether the packet was received
  int packetID; // Number from 0 to (WINDOW_SIZE * 2) - 1
  int dataSize; // Number of bytes of data in packet
  char packetData[MAX_DATA_SIZE]; // Data in packet
} Packet;

// Prototype for function to find parity of n characters in packet
int getParity(char *packet, int n);

int main(int argc, char** argv)
{
  int sockfd;
  char ipAddress[100];
  int portNumber;
  struct sockaddr_in serveraddr;
  char file[512];
  char *fileName, *fileExtension;
  char outputFile[512];
  FILE *fp;
  struct timeval timeout;
  socklen_t len;
  Packet packet[WINDOW_SIZE];
  char packetSend[MAX_PACKET_SIZE];
  char packetRcvd[MAX_PACKET_SIZE];
  char packetType[TYPE_SIZE+1];
  int parity, parityRcvd;
  int dataSize;
  int windowStart;
  int numPackets;
  int i, j, n;
  int checkID, start, end, resendAck;
  char ack[2];
  char *tok;
  int errors = 0;

  // Check for correct number of command line arguments
  if(argc != 4){
    printf("Syntax: ./udpclient <IP address> <port number> <filename>\n");
    return -1;
  }

  // Store command line arguments
  strcpy(ipAddress, argv[1]);
  portNumber = atoi(argv[2]);
  strcpy(file, argv[3]);

  // Check that a valid port number was entered
  if(portNumber < 1024){
    printf("Invalid Port Number\n");
    return -2;
  }

  // Create an end point for communication
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);

  if(sockfd < 0){
    printf("There was an error creating the socket\n");
    return -1;
  }

  // Set variables of server address structure
  serveraddr.sin_family = AF_INET; // Internet socket
  serveraddr.sin_port = htons(portNumber); // Port Number
  serveraddr.sin_addr.s_addr = inet_addr(ipAddress); // IP Address

  // Set the socket receive timeout to 0.5 seconds
  timeout.tv_sec = 0;
  timeout.tv_usec = 500000;
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  // Send file to server
  while(1){
    strcpy(packetType, "FILE");
    dataSize = strlen(file) + 1;
    memcpy(packetSend + PACKET_TYPE_SHIFT, packetType, TYPE_SIZE);
    memcpy(packetSend + DATA_SIZE_SHIFT, &dataSize, sizeof(int));
    memcpy(packetSend + DATA_SHIFT, file, dataSize);    
    parity = getParity(packetSend, PARITY_SHIFT);
    memcpy(packetSend + PARITY_SHIFT, &parity, sizeof(int));    

    // Send file to the server
    sendto(sockfd, packetSend, MAX_PACKET_SIZE, 0,
	   (struct sockaddr*)&serveraddr, sizeof(serveraddr)); 

    // Wait for ACK or start of data
    n = recvfrom(sockfd, packetRcvd, MAX_PACKET_SIZE, 0,
		 (struct sockaddr*)&serveraddr, &len);

    // Check for timeout
    if(n == -1){
      continue;
    }

    memcpy(&parityRcvd, packetRcvd + PARITY_SHIFT, sizeof(int));      

    parity = getParity(packetRcvd, PARITY_SHIFT);

    // Ignore packet if parity is incorrect
    if(parityRcvd != parity){
      printf("Parity error found.\n");
      errors++;
      continue;
    }

    // If server sends back ACK or starts sending data, break out of loop
    memcpy(packetType, packetRcvd + PACKET_TYPE_SHIFT, TYPE_SIZE);
    packetType[4] = '\0';
    if(strstr(packetType, "ACK") || strstr(packetType, "DATA")){
      break;
    }
  }

  // Open file to save copy to
  fileName = strtok(file, ".");
  fileExtension = strtok(NULL, ".");
  sprintf(outputFile, "%s_Copy.%s", fileName, fileExtension);
  fp = fopen(outputFile, "wb");

  // Set all counters and flags to zero
  windowStart = 0;
  i = 0;
  for(j = 0; j < WINDOW_SIZE; j++){
    packet[j].recvFlag = 0;
  }

  // Set the socket receive timeout to 10 seconds
  timeout.tv_sec = 10;
  timeout.tv_usec = 0;
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  // Loop to receive all packets
  while(1){

    // Receive file packets from server
    n = recvfrom(sockfd, packetRcvd, MAX_PACKET_SIZE, 0,
		 (struct sockaddr*)&serveraddr, &len);

    // Exit if data transfer times out
    if(n == -1){
      
      // Check if file transfer is complete in case DONE packet got lost
      if(windowStart == numPackets){
	printf("File transfer complete\nFile saved: %s\n", outputFile);
	break;
      }

      printf("Data transfer timed out\n");
      break;
    }

    memcpy(&parityRcvd, packetRcvd + PARITY_SHIFT, sizeof(int));      

    parity = getParity(packetRcvd, PARITY_SHIFT);

    // Ignore packet if parity is incorrect
    if(parityRcvd != parity){
      printf("Parity error found.\n");
      errors++;
      continue;
    }

    // Check packet type
    memcpy(packetType, packetRcvd + PACKET_TYPE_SHIFT, TYPE_SIZE);
    packetType[4] = '\0';

    // If file transfer is done, break out of loop
    if(strstr(packetType, "DONE")){
      printf("File transfer complete\nFile saved: %s\n", outputFile);
      break;
    }

    // Ignore any packets that are not data
    if(!strstr(packetType, "DATA")){
      continue;
    }

    // Check for duplicate ID
    memcpy(&checkID, packetRcvd + PACKET_ID_SHIFT, sizeof(int));

    // Reset resend flag
    resendAck = 0;

    // Get start and end of window with values between 0 and 9
    start = windowStart%(2*WINDOW_SIZE);
    end = (windowStart+(WINDOW_SIZE-1))%(2*WINDOW_SIZE);

    // Check if packet is duplicate
    if(start <= WINDOW_SIZE){ // Window does not wrap
      if(checkID < start || checkID > end){ // Packet not within window
	resendAck = 1;
      }
      else{ // Packet is within window
	if(packet[checkID%WINDOW_SIZE].recvFlag){ // Packet already received
	  resendAck = 1;
	}
      }
    }
    else{ // Window wraps
      if(checkID < start && checkID > end){ // Packet not within window
	resendAck = 1;
      }
      else{ // Packet is within window
	if(packet[checkID%WINDOW_SIZE].recvFlag){ // Packet already received
	  resendAck = 1;
	}
      }
    }

    // Resend ack if duplicated packet was received
    if(resendAck){      
      printf("Resending ack %d\n", checkID);

      strcpy(packetType, "ACK");
      memcpy(packetSend + PACKET_TYPE_SHIFT, packetType, TYPE_SIZE);
      memcpy(packetSend + PACKET_ID_SHIFT, &checkID, sizeof(int));
      parity = getParity(packetSend, PARITY_SHIFT);
      memcpy(packetSend + PARITY_SHIFT, &parity, sizeof(int));

      sendto(sockfd, packetSend, MAX_PACKET_SIZE, 0,
	     (struct sockaddr*)&serveraddr, sizeof(serveraddr));

      continue;
    }

    i = checkID % WINDOW_SIZE;

    // Get packet information
    packet[i].recvFlag = 1;
    memcpy(&(packet[i].packetID), packetRcvd + PACKET_ID_SHIFT, sizeof(int));
    memcpy(&numPackets, packetRcvd + NUM_PACKETS_SHIFT, sizeof(int));
    memcpy(&(packet[i].dataSize), packetRcvd + DATA_SIZE_SHIFT, sizeof(int));
    memcpy(packet[i].packetData, packetRcvd + DATA_SHIFT, MAX_DATA_SIZE);

    printf("Packet ID %d received\n", packet[i].packetID);

    // Write data to file in order
    for(j = 0; j < WINDOW_SIZE; j++){
      if(packet[j].packetID == windowStart % (WINDOW_SIZE * 2) && packet[j].recvFlag){
	fwrite(packet[j].packetData, packet[j].dataSize, 1, fp);
	windowStart++;
	packet[j].recvFlag = 0;
	j = 0;
	printf("Wrote packet %d to file\n", windowStart-1);
      }
    }

    // Send acknowledgement back
    strcpy(packetType, "ACK");
    memcpy(packetSend + PACKET_TYPE_SHIFT, packetType, TYPE_SIZE);
    memcpy(packetSend + PACKET_ID_SHIFT, &(packet[i].packetID), sizeof(int));
    parity = getParity(packetSend, PARITY_SHIFT);
    memcpy(packetSend + PARITY_SHIFT, &parity, sizeof(int));
    
    sendto(sockfd, packetSend, MAX_PACKET_SIZE, 0,
	     (struct sockaddr*)&serveraddr, sizeof(serveraddr));
  }

  printf("Errors: %d\n", errors);

  fclose(fp);

  return 0;
}

// Function to find parity of n characters in packet
int getParity(char *packet, int n){
  int i, j;
  char c;
  int parity = 0;

  // Loop through each character
  for(i = 0; i < n; i++){
    c = packet[i];

    // Loop through each bit
    for(j = 0; j < 8; j++){
      // Check if bit is 1
      if(c & (1 << j)){
	parity++; // Increment counter
      }
    }
  }

  // Return 0 for even, 1 for odd
  return parity % 2;
}
