#include <sys/time.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>


// As we are a synchrounous app, it is fine to share a buffer for the whole program's lifetime.
#define BUFFER_SIZE 1024
static char buffer[BUFFER_SIZE] = {0};

// Array that will contains the custom MOTD containing the client's IP.
#define JSON_BUFFER_SIZE 255
static char json_response_buffer[JSON_BUFFER_SIZE];

// Will be incremented by 1 each time a new client tries to connect.
static unsigned long connection_count = 0;

// The time waiting for a new packet from the client before timing out.
static const unsigned long RECV_TIMEOUT_MS = 200;

// Since we are handling ONE connection at a time, this is valid
// The state switches from Handshake status to Status.
static bool is_state_status = false;

char *get_client_ip(int client_sockfd) {
    struct sockaddr_in client_addr;
    socklen_t addr_size = sizeof(client_addr);

    if (getpeername(client_sockfd, (struct sockaddr *) &client_addr, &addr_size) == -1) {
        perror("getpeername failed");
        return "NULL";
    }

    // Convert the IP address to a string and return it
    char *client_ip = inet_ntoa(client_addr.sin_addr);
    return client_ip;
}

int log_ip(const char *ipAddress) {
    const char *filename = "logged_ips.txt";
    FILE *file = fopen(filename, "a"); // Open file in appending mode.
    if (file == NULL) {
        perror("Failed to open file");
        return -1;
    }

    // obtain and format the current time
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    char formatted_time[100];
    strftime(formatted_time, sizeof(formatted_time), "[%Y-%m-%d %H:%M:%S] - ", tm_now);

    // prepare the final string with a newline
    size_t ip_len = strlen(ipAddress);
    size_t time_len = strlen(formatted_time);
    size_t total_len = time_len + ip_len + 2; // include '\n' and '\0'
    char *line = malloc(total_len); // dynamically allocate to fit the full line
    if (line == NULL) {
        perror("Failed to allocate memory");
        fclose(file);
        return -1;
    }

    // Construct the line to be logged
    strcpy(line, formatted_time);
    strcat(line, ipAddress);
    strcat(line, "\n"); // Append newline

    // Write to the file
    fputs(line, file);
    fclose(file);

    // Cleanup and log success
    free(line);
    printf("IP logged successfully in '%s'.\n", filename);
    return 0;
}

int get_server_socket(unsigned short port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // Set this chunk of memory to be zeroes
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY; // listen on any network interface

    if (bind(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        printf("--> [get_server_socket] Closed socket\n");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

// Listens on server socket and returns client socket when there is a connection
// and set a timeout.
int get_client_socket(int sockfd) {
    if (listen(sockfd, 15) < 0) {
        perror("[get_client_socket] Error on listen");
        return -1;
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Attempt to accept a new connection
    int client_sock = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
    if (client_sock < 0) {
        perror("[get_client_socket] Error on accept");
        return -1;
    }

    // Set a timeout for recv on the client socket
    struct timeval timeout;
    timeout.tv_sec = 0;                       // Seconds
    timeout.tv_usec = RECV_TIMEOUT_MS * 1000;  // RECV_TIMEOUT in microseconds
    
    if (setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("[get_client_socket] Error setting timeout");
        close(client_sock);
        printf("--> [get_client_socket] Closed socket\n");
        return -1;
    }

    // Return the new socket descriptor for the accepted connection
    return client_sock;
}

// Listens for a packet from socket. A timeout is set from the get_client_socket() function.
// If the socket does not respond within the timeframe, it returns -1
ssize_t receive_from_socket(int sockfd, char *buffer, size_t buffer_size) {
    ssize_t bytes_received = recv(sockfd, buffer, buffer_size, 0);

    if (bytes_received >= 0) {
        return bytes_received;
    }


    // From now on, we know recv() returned -1 (an error)


    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Timout occurred
        fprintf(stderr, "[receive_from_socket] recv() timeout: No response within the timeframe\n");
    } else {
        perror("[receive_from_socket] recv() failed");
    }

    return bytes_received;
}

int handle_status_request(int sockfd, ssize_t data_size) {
    if (data_size <= 0) {
        perror("[handle_status_request] Data size negative or zero");
        return -1;
    }

    // https://wiki.vg/Protocol#Status_Request
    // That is, Lenght (1 Byte) + PacketId (1 Byte)
    if (data_size != 2) {
        fprintf(stderr, "[handle_status_request] Packet is not 2 bytes long\n");
    }

    unsigned char packet_id = 0x00;
    unsigned char response_length = (unsigned char) strlen(json_response_buffer);
    unsigned char length = 1 + 1 + response_length; // packet_id (1 byte) + length byte (1 byte) + response
    unsigned char total_packet_length = 1 + length; // length byte + the actual length
    unsigned char status_response_packet[total_packet_length];

    status_response_packet[0] = length;
    status_response_packet[1] = packet_id;
    status_response_packet[2] = response_length;

    memcpy(status_response_packet + 3, json_response_buffer, response_length); // Pack everything into the packet.

    ssize_t sent_bytes = send(sockfd, status_response_packet, total_packet_length, 0);
    if (sent_bytes != total_packet_length) {
        perror("[handle_status_request] Failed to send the packet");
        return -1;
    }

    printf("[handle_status_request] Successfully sent the Status Response packet. Sent %zd bytes!\n", sent_bytes);


    return 0;
}

// Sends the Ping Response packet to the socket which is the same as the received Ping Request
// packet the client sent.
int handle_ping(int sockfd, const char *buffer, ssize_t data_size) {
    // Packet Length (PacketID len + Payload len) which should be 9.
    unsigned char length = buffer[0];

    // Warnings
    if (length != 9) {
        perror("[handle_ping] Ping Request packet is not of length 9!");
    }

    // Send back the client's packet
    ssize_t bytes_transferred = send(sockfd, buffer, data_size, 0);
    if (bytes_transferred == -1) {
        perror("[handle_ping] send() failed, got -1 bytes sent");
        return -1;
    }

    printf("[handle_ping] Successfully sent the Ping Response packet. Sent %ld bytes\n", bytes_transferred);

    return 0;
}



void update_json_response(const char* ip) {
    int max_players = 8;
    int online_players = 6;
    char* description_template = "Your IP is: %s";

    // Prepare the description with IP
    char description[50];
    snprintf(description, sizeof(description), description_template, ip);

    const char* json = "{\"version\":{\"name\":\"1.21\",\"protocol\":767},\"players\":{\"max\":%d,\"online\":%d},\"description\":{\"text\":\"%s\"}}";

    // Static buffer to hold the generated JSON
    static char buffer[200];  // Adjust this size if needed
    int bytes_written = snprintf(buffer, sizeof(buffer), json, max_players, online_players, description);

    if (bytes_written < 0) {
        printf("Failed to write MOTD/description into buffer. Aborting...\n");
        exit(EXIT_FAILURE);
    }


    // Check if the resulting JSON is longer than the buffer size
    if (bytes_written >= (int)sizeof(buffer)) {
        printf("JSON response is too long. Required: %d, Buffer size: %zu. Aborting...\n", 
               bytes_written, sizeof(buffer));
        exit(EXIT_FAILURE);
    }

    // Check if the JSON exceeds 0111 1111 which is 127 bytes (VarInt limitation)
    // And we have not implented VarInts.
    if (bytes_written > (0xFF >> 1)) {  // 127
        printf("Response JSON length is greater than 127, undefined behavior, aborting...\n");
        exit(EXIT_FAILURE);
    }

    // Safely copy the generated JSON into the final json_response_buffer
    snprintf(json_response_buffer, sizeof(json_response_buffer), "%s", buffer);
}


// Called when a handshake packet is detected.
//
// Logs IP and updates the MOTD with the IP.
void handle_handshake(int sockfd, const char *buffer, ssize_t data_size) {
    unsigned char next_state = buffer[data_size - 1]; // last byte is next state. '-1' : 0 indexed arrays

    if (next_state != 1) {
        printf("[handle_handshake] Next state is not status\n");
        return;
    }

    // Get IP + log IP + update MOTD
    // when we are sure that the client is sending a Handshake packet.
    const char *client_ip = get_client_ip(sockfd);
    log_ip(client_ip);
    update_json_response(client_ip);

    // The state switches to Status just after the handshake.
    is_state_status = true;
}


// Dispatches the packet by parsing its PacketID.
//
// 0 -> Handshake packet.
// 1 -> Ping Request packet.
void handle_packet(int sockfd, const char *buffer, ssize_t data_size) {
    // Standard packet layout:
    // Length [VarInt](PacketID len + Payload len)
    // PacketID [VarInt]
    // Data [Byte Array]

    // Since we are only interested in Handshake packets, the Length will always be strictly less
    // than 128, thus, we are certain the Length in contained in the first byte only.
    unsigned char packet_id = buffer[1];

    // TODO: Switch this logic. Only respond to the client when it sends
    // the Status Request packet.
    // And the Handshake packet only switches the global state flag!!

    switch (packet_id) {
        case 0x00:
            // We are in the Handshake state.
            if (!is_state_status) {
                printf("Packet type: Handshake\n");
                handle_handshake(sockfd, buffer, data_size);
                return;
            }

            // If the flag is set to true, we are in the Status state,
            // we will respond to the client.
            printf("Packet type: Status Request\n");
            handle_status_request(sockfd, data_size);
            break;
        case 0x01:
            printf("Packet type: Ping Request\n");
            handle_ping(sockfd, buffer, data_size);
            // Connection is closed in the caller funcion (begin_listen)
            break;
        default:
            printf("Packet type: unknown\n");
            break;
    }
}


unsigned short get_port(int argc, char* argv[]) {
    const unsigned short DEFAULT_PORT = 25565;

    char* error_message = "Invalid CLI option. Use ./honeypot -p <port> (or --port)\n";

    if (argc == 1) {
        return DEFAULT_PORT;
    }

    // If there are more than 3 args, it's malformed.
    if (argc != 3) {
        printf("%s\n", error_message);
        exit(EXIT_FAILURE);
    }


    // ./honeypot --port 29844
    if (strcmp(argv[1], "-p") != 0 && strcmp(argv[1], "--port") != 0) {
        printf("%s\n", error_message);
        exit(EXIT_FAILURE);
    }

    char* endptr;

    // Attempt to convert the first string
    unsigned long number = strtoul(argv[2], &endptr, 10);
    if (endptr == argv[2] || number > 65535) {
        printf("Invalid port range. (Range is from 0 to 65,535)\n");
        exit(EXIT_FAILURE);
    }

    return (unsigned short)number;
}


// Listens for incomming packets on the socket.
int handle_connection(int sockfd) {
    // Infinite loop for packets
    while (1) {
        // Wait for new packets...
        ssize_t read_bytes = receive_from_socket(sockfd, buffer, sizeof(buffer));


        if (read_bytes > 0) {
            printf("Received %ld bytes\n", read_bytes);
            handle_packet(sockfd, buffer, read_bytes);

        } else if (read_bytes == 0) {
            // Connection closed by client
            return read_bytes;

        } else {
            // Got a negative amount
            printf("Error receiving data, got %ld bytes\n", read_bytes);
            return read_bytes;
        }
    }
}


// Infinitely listening for connections.
void begin_listen(int server_sockfd) {
    // Infinite loop for connections
    while (1) {
        // Wait for new connection...
        int client_sockfd = get_client_socket(server_sockfd);


        // Print new connection
        printf("\n********************%lu********************\n\n", connection_count);
        connection_count++;

        if (handle_connection(client_sockfd) == 0) {
            printf("--> Connection closed by client\n");
        } else {
            printf("--> Connection closed\n");
        }

        close(client_sockfd);

        // Reset the global state.
        is_state_status = false;
    }
}


int main(int argc, char **argv) {
    unsigned short listening_port = get_port(argc, argv);
    printf("Listening on 0.0.0.0:%d...\n", listening_port);

    int server_sockfd = get_server_socket(listening_port);

    // This function contains an infinite loop.
    begin_listen(server_sockfd);

    close(server_sockfd);
    return 0;
}
