/*****************************************
 * Caleb Hayward
 * Justin Novack
 * 2/21/2017
 * CIS457 - Project #2
 * Reliable File Transfer over UDP
 * udpserver.c
 ******************************************/

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PACKET_TYPE_SHIFT 0
#define PACKET_ID_SHIFT 4
#define NUM_PACKETS_SHIFT 8
#define DATA_SIZE_SHIFT 12
#define DATA_SHIFT 20
#define PARITY_SHIFT 1020

#define MAX_PACKET_SIZE 1024
#define MAX_DATA_SIZE 1000
#define WINDOW_SIZE 5
#define TYPE_SIZE 4

// Packet structure
typedef struct{
  int ackRcvd; // Flag to indicate whether the packet was acknowledged
  int packetID; // Number from 0 to (WINDOW_SIZE * 2) - 1
  int dataSize; // Number of bytes of data in packet
  char packet[MAX_PACKET_SIZE]; // Packet including header and data
  char packetData[MAX_DATA_SIZE]; // Data in packet
} Packet;

// Prototype for function to find parity of n characters in packet
int getParity(char *packet, int n);

int main(int argc, char **argv)
{
  int sockfd;
  int portNumber;
  struct sockaddr_in serveraddr, clientaddr;
  socklen_t len;
  FILE *fp;
  char packetSend[MAX_PACKET_SIZE];
  char packetRcvd[MAX_PACKET_SIZE];
  char packetType[TYPE_SIZE+1];
  int parity, parityRcvd;
  char file[512];
  int fileSize;
  int n, i, j;
  struct timeval timeout;
  Packet packet[WINDOW_SIZE];
  int numPackets;
  int packetsSent = 0, windowStart = 0;
  int ackID;
  int start, end;
  int errors;

  // Check for correct number of command line arguments
  if(argc != 2){
    printf("Syntax: ./udpserver <port number>\n");
    return -1;
  }

  // Get port number from command line argument
  portNumber = atoi(argv[1]);

  // Check that a valid port number was entered
  if(portNumber < 1024){
    printf("Invalid Port Number\n");
    return -2;
  }

  // Create an end point for communication
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(sockfd < 0){
    printf("There was an error creating the socket\n");
    return -3;
  }

  // Set variables of server address structure
  serveraddr.sin_family = AF_INET; // Internet socket
  serveraddr.sin_port = htons(portNumber); // Port Number
  serveraddr.sin_addr.s_addr = INADDR_ANY; // Connect to any address

  // Bind name to the socket
  bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr));

  // Set the socket receive timeout to 0.5 seconds
  timeout.tv_sec = 0;
  timeout.tv_usec = 500000;
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  // Run in loop to keep server open
  while(1){
    errors = 0;   
 
    len = sizeof(clientaddr);
	
    // Wait to receive file name from client
    while(1){
      recvfrom(sockfd, packetRcvd, MAX_PACKET_SIZE, 0,
	       (struct sockaddr*)&clientaddr, &len);
      
      memcpy(packetType, packetRcvd + PACKET_TYPE_SHIFT, TYPE_SIZE);
      memcpy(&parityRcvd, packetRcvd + PARITY_SHIFT, sizeof(int));      

      parity = getParity(packetRcvd, PARITY_SHIFT);

      // Ignore packet if parity is incorrect
      if(parityRcvd != parity){
	printf("Parity error found.\n");
	errors++;
	continue;
      }

      // Check that packet contains file name
      packetType[4] = '\0';      
      if(!strstr(packetType, "FILE")){
	continue;
      }
      
      memcpy(&fileSize, packetRcvd + DATA_SIZE_SHIFT, sizeof(int));
      memcpy(file, packetRcvd + DATA_SHIFT, fileSize);
      file[fileSize] = '\0';

      // Send acknowledgement back
      strcpy(packetType, "ACK");
      memcpy(packetSend + PACKET_TYPE_SHIFT, packetType, TYPE_SIZE);
      parity = getParity(packetSend, PARITY_SHIFT);
      memcpy(packetSend + PARITY_SHIFT, &parity, sizeof(int));      

      sendto(sockfd, packetSend, MAX_PACKET_SIZE, 0,
	     (struct sockaddr*)&clientaddr, sizeof(clientaddr));

      break;
    }

    printf("Filename: %s\n", file);
	  
    // Open up binary file to read
    fp = fopen(file, "rb");
    if(fp == NULL){
      strcpy(packetType, "DONE");
      memcpy(packetSend + PACKET_TYPE_SHIFT, packetType, TYPE_SIZE);
      parity = getParity(packetSend, PARITY_SHIFT);
      memcpy(packetSend + PARITY_SHIFT, &parity, sizeof(int));
      sendto(sockfd, packetSend, MAX_PACKET_SIZE,
	     0, (struct sockaddr*)&clientaddr, sizeof(clientaddr));
      continue;
    }
	  
    // Find size of the file
    fseek(fp, 0, SEEK_END);
    fileSize = ftell(fp);
    rewind(fp);

    // Calculate number of packets needed to send all data
    numPackets = (fileSize / MAX_DATA_SIZE) + 1;

    // Set all counters and flags to zero
    packetsSent = 0;
    windowStart = 0;
    for(i = 0; i < WINDOW_SIZE; i++){
      packet[i].ackRcvd = 0;
    }

    // Send first window of packets
    for(i = 0; i < WINDOW_SIZE; i++){

      // Check if all the packets have been sent
      if(packetsSent == numPackets){
	break;
      }

      packet[i].packetID = i;

      // Read the file data
      packet[i].dataSize = fread(packet[i].packetData, 1, MAX_DATA_SIZE, fp);

      // Copy header info and data into packet
      strcpy(packetType, "DATA");
      memcpy(packet[i].packet + PACKET_TYPE_SHIFT, packetType, TYPE_SIZE);
      memcpy(packet[i].packet + PACKET_ID_SHIFT, &(packet[i].packetID), sizeof(int));
      memcpy(packet[i].packet + NUM_PACKETS_SHIFT, &numPackets, sizeof(int));
      memcpy(packet[i].packet + DATA_SIZE_SHIFT, &(packet[i].dataSize), sizeof(int));
      memcpy(packet[i].packet + DATA_SHIFT, packet[i].packetData, MAX_DATA_SIZE);
      parity = getParity(packet[i].packet, PARITY_SHIFT);
      memcpy(packet[i].packet + PARITY_SHIFT, &parity, sizeof(int));

      // Send packet to client
      printf("Sending packet %d (ID: %d)\n", packetsSent, packet[i].packetID);
      sendto(sockfd, packet[i].packet, sizeof(packet),
	     0, (struct sockaddr*)&clientaddr, sizeof(clientaddr));
      
      // Increment number of packets sent
      packetsSent++;
    }

    // Loop to slide window as packets are acknowledged
    while(1){

      // Receive acknowledgement from client
      n = recvfrom(sockfd, packetRcvd, MAX_PACKET_SIZE, 0,
		   (struct sockaddr*)&clientaddr, &len);

      // Check for timeout
      if(n == -1){
	printf("Timeout: Acknowledge not received\n");

	// Resend all unacknowledged packets in window
	for(i = 0; i < WINDOW_SIZE; i++){
	  if(windowStart+i >= numPackets){
	    continue;
	  }

	  if(!packet[(windowStart+i)%WINDOW_SIZE].ackRcvd){
	    printf("Resending packet %d (ID: %d)\n",
		   windowStart+i, packet[(windowStart+i)%WINDOW_SIZE].packetID);
	    sendto(sockfd, packet[(windowStart+i)%WINDOW_SIZE].packet,
		   sizeof(packet[(windowStart+i)%WINDOW_SIZE].packet), 0,
		   (struct sockaddr*)&clientaddr, sizeof(clientaddr));
	  }
	}
	continue;
      }
      else{
	memcpy(packetType, packetRcvd + PACKET_TYPE_SHIFT, TYPE_SIZE);
	packetType[4] = '\0';

	// Ignore packet if not ACK
	if(!strstr(packetType, "ACK")){
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

	memcpy(&ackID, packetRcvd + PACKET_ID_SHIFT, sizeof(int));
	printf("Ack received for packet %d\n", ackID);
      }

      // Get start and end of window with values between 0 and 9
      start = windowStart%(2*WINDOW_SIZE);
      end = (windowStart+(WINDOW_SIZE-1))%(2*WINDOW_SIZE);

      // Check if ack is duplicate
      if(start <= WINDOW_SIZE){ // Window does not wrap
	if(ackID < start || ackID > end){ // Packet not within window
	  continue;
	}
	else{ // Packet is within window
	  if(packet[ackID%WINDOW_SIZE].ackRcvd){ // Ack already received
	    continue;
	  }
	}
      }
      else{ // Window wraps
	if(ackID < start && ackID > end){ // Packet not within window
	  continue;
	}
	else{ // Packet is within window
	  if(packet[ackID%WINDOW_SIZE].ackRcvd){ // Ack already received
	    continue;
	  }
	}
      }

      // Set flag to indicate packet was acknowledged
      packet[ackID % WINDOW_SIZE].ackRcvd = 1;

      j = windowStart % WINDOW_SIZE;

      // Slide window and send new packets
      for(i = 0; i < WINDOW_SIZE; i++){	

	// Check if packet was acknowledged
	if(packet[j].ackRcvd){

	  // Reset packet acknowledge flag
	  packet[j].ackRcvd = 0;

	  // Check if all the packets have already been sent
	  if(packetsSent == numPackets){
	    j = (j + 1) % WINDOW_SIZE;
	    windowStart++;
	    continue;
	  }

	  // Set ID of next packet
	  packet[j].packetID = packetsSent % (WINDOW_SIZE * 2);

	  // Read the file data
	  packet[j].dataSize = fread(packet[j].packetData, 1, MAX_DATA_SIZE, fp);
     
	  // Copy header info and data into packet
	  strcpy(packetType, "DATA");
	  memcpy(packet[j].packet + PACKET_TYPE_SHIFT, packetType, TYPE_SIZE);
	  memcpy(packet[j].packet + PACKET_ID_SHIFT, &(packet[j].packetID), sizeof(int));
	  memcpy(packet[j].packet + NUM_PACKETS_SHIFT, &numPackets, sizeof(int));
	  memcpy(packet[j].packet + DATA_SIZE_SHIFT, &(packet[j].dataSize), sizeof(int));
	  memcpy(packet[j].packet + DATA_SHIFT, packet[j].packetData, MAX_DATA_SIZE);
	  parity = getParity(packet[j].packet, PARITY_SHIFT);
	  memcpy(packet[j].packet + PARITY_SHIFT, &parity, sizeof(int));

	  // Send next packet to client
	  printf("Sending packet %d (ID: %d)\n", packetsSent, packet[j].packetID);
	  sendto(sockfd, packet[j].packet, sizeof(packet[j].packet),
		 0, (struct sockaddr*)&clientaddr, sizeof(clientaddr));
	  
	  // Increment number of packets sent
	  packetsSent++;

	  // Slide window
	  windowStart++; 
	}
	else{
	  break;
	}

	j = (j + 1) % WINDOW_SIZE;
      }

      // Check if all packets have been acknowledged
      if(windowStart == numPackets){
	
	// Send DONE packet to client
	strcpy(packetType, "DONE");
	memcpy(packetSend + PACKET_TYPE_SHIFT, packetType, TYPE_SIZE);
	parity = getParity(packetSend, PARITY_SHIFT);
	memcpy(packetSend + PARITY_SHIFT, &parity, sizeof(int));
	sendto(sockfd, packetSend, MAX_PACKET_SIZE,
	       0, (struct sockaddr*)&clientaddr, sizeof(clientaddr));

	fclose(fp);	
	printf("File transfer complete\n");
	break;
      }
    }

    printf("Errors: %d\n", errors);
  }

  close(sockfd);
  
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
