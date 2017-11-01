# Design Overview

The purpose of this project is to reliably transfer a file over UDP by implementing sliding window protocol and accounting for data loss, duplication, reordering, and corruption.

# Packet Structure
Each packet always consists of 1024 bytes with the structure shown below:
Type    ID    # Packets   Data    Size    Unused    Data    Parity
 0      4         8        12      16       20      1020    1024

Type - 4 byte character array indicating the kind of packet
   FILE - packet used to send the name of the file to transfer
   DATA - packet used to transfer the file data from the server to client
   ACK - packet used to acknowledge that another packet was received
   DONE - packet used to indicate the file transfer is complete
ID - Integer between 0 and 9 based on where the sliding window is
Packets - Integer indicating the total number of packets needed to transfer all the data in the file window is
Data Size - Integer indicating the number of bytes used in the data section of the packet
Unused - 4 bytes of unused space reserved for if additional information needs to be added to the header
Data - 1000 byte character array reserved for storing the data to send
Parity - Integer that contains the parity of the first 1020 bytes, used for error checking

# Implementing a Sliding Window
To begin, the server receives a FILE packet from a client containing the name of a file and then sends an ACK packet back to the client. If the server cannot open the file, it sends a DONE command to the client. If the server can open the file, it begins reading in the file and sending DATA packets to the client. Since the size of the sliding window is five packets, each packet is assigned an ID number between 0 and 9. The first five data packets are sent by the server. Each time the client receives a DATA packet, it sets a flag and sends an ACK packet back to the server indicating the packet ID of the packet received. When the server receives an ACK packet from the client, it will then set a flag indicating that that packet has been acknowledged. For the client, once the first packet of the sliding window has been received, it will write the data to a new file and slide the window. For the server, once the first packet of the sliding window has been acknowledged, the window will slide by one and the next packet will be sent. Once all the packets have been sent, the server will not send any new packets and will wait until all packets sent have been acknowledged by the client. Once all have been acknowledged, the server will send a DONE packet to the client indicating that the file transfer has been complete.

# Accounting for Packet Loss
To detect packet loss, a timeout of 0.5 seconds is used when waiting for an ACK packet to be received. When the initial FILE packet is sent by the client, if an ACK is not received from the server within 0.5 seconds, the client will resend the FILE packet. If the ACK packet from the server to the client is lost, the server will start sending DATA packets and the client will know the server received the FILE packet. If a timeout occurs during data transfer, the server will resend all unacknowledged packets within the sliding window. This deals with both possibilities of the DATA packets from the server to client getting lost and the ACK packets from the client to server getting lost. If the DONE packet is lost when sent from the server to client at the end of the file transfer, the client will timeout. If the client has received all the packets and times out, it knows the file transfer is complete.

# Accounting for Packet Duplication
To detect packet duplication during the file transfer, the packet type and packet ID is checked to ensure that the expected packets was received. If the packet type is not what was expected, the packet is ignored. If the packet ID is not within the sliding window or if the packet ID is within the sliding window but has already been received, the packet is a duplicate. If the client receives a duplicate DATA packet from the server, it ignores the data but resends an ACK packet in case the last ACK packet it sent for that data was lost. If the server receives a duplicate ACK packet from the client, it simply ignores it.  

# Accounting for Packet Reordering
To solve the problem of packets being received out of order, an array is used to store packets with a size the same amount as the window size. The packet ID is used here to determine which index of the array the packet should be stored in. The program stores the packet at location Packet[packetID % WINDOW_SIZE] so that the packet storage location is based on the location in the sliding window. This means: 

Packet[0] stores ID 0 or 5
Packet[1] stores ID 1 or 6
Packet[2] stores ID 2 or 7
Packet[3] stores ID 3 or 8
Packet[4] stores ID 4 or 9

This method allows for packets to be reordered based on their packet ID and does not allow for multiple packets with the same packet ID because it stores the packet ID as a number between 0 and 9, which is two times the window size.

# Accounting for Packet Corruption
Before sending a packet, the parity of the first 1020 bytes of the packet is calculated and stored as an integer at the end of the packet. When a packet is received, the same parity calculation is performed for the first 1020 bytes and compared to the parity stored at the end of the packet. If the parity check fails, the packet was corrupted and it is ignored. This method of checking for packet corruption will catch any odd amount of bit errors.
