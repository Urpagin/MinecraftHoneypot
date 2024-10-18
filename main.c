#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

static char json_response_buffer[0xFF]; // 255

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

int log_ip(const char *ip) {
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
    size_t ip_len = strlen(ip);
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
    strcat(line, ip);
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
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // set the memory to all zeroes.
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY; // listen on any network interface

    if (bind(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

// listens on server socket and returns client socket when connection.
int get_client_socket(int sockfd) {
    if (listen(sockfd, 15) < 0)
        return -1;

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // attempt to accept a new connection
    int client_socket = accept(sockfd, (struct sockaddr *) &client_addr, &client_len);
    if (client_socket < 0) {
        perror("accept failed");
        return -1;
    }

    return client_socket; // return the new socket descriptor for the accepted connection
}

ssize_t receive_from_socket(int sockfd, char *buffer, size_t buffer_size) {
    ssize_t bytes_received = recv(sockfd, buffer, buffer_size, 0);
    if (bytes_received < 0) {
        perror("recv failed");
        return -1;
    }

    return bytes_received;
}

int handle_handshake(int sockfd, const char *buffer, ssize_t data_size) {
    if (data_size <= 0) {
        perror("[handshake] data size negative or zero");
        return -1;
    }
    unsigned char next_state = buffer[data_size - 1]; // last byte is next state. '-1' : 0 indexed arrays
    printf("nextstate: %d\n", (int) next_state);
    if (next_state != 1) {
        printf("[handshake] next state is not status\n");
        return -1;
    }

    unsigned char packet_id = 0x00;
    unsigned char response_length = (unsigned char) strlen(json_response_buffer);
    unsigned char length = 1 + 1 + response_length; // packet_id (1 byte) + length byte (1 byte) + response
    unsigned char total_packet_length = 1 + length; // length byte + the actual length
    unsigned char status_response_packet[total_packet_length];

    status_response_packet[0] = length;
    status_response_packet[1] = packet_id;
    status_response_packet[2] = response_length;

    memcpy(status_response_packet + 3, json_response_buffer, response_length); // put all string into packet.

    ssize_t sent_bytes = send(sockfd, status_response_packet, total_packet_length, 0);
    if (sent_bytes != total_packet_length) {
        perror("send failed");
        return -1;
    } else {
        printf("[handshake] Sent %d bytes!\n", (int) sent_bytes);
    }
    return 0;
}

int handle_ping(int sockfd, const char *buffer, ssize_t data_size) {
    if (data_size < 9) { // Ensure there's enough data for one byte of length + 8 bytes of long
        return -1;
    }

    unsigned char length = buffer[0]; // Assuming this is the length of the following payload (which should be 8 here)

    ssize_t bytes_transferred = send(sockfd, buffer, data_size, 0);
    if (bytes_transferred != length + 1) {
        perror("send failed");
        return -1;
    }
    printf("sent %d bytes\n", (int) bytes_transferred);

    return 0;
}

void handle_packet(int sockfd, const char *buffer, ssize_t data_size) {
    unsigned char length = buffer[0];
    unsigned char packet_id = buffer[1];

    switch (packet_id) {
        case 0x00:
            printf("Packet type: Handshake\n");
            handle_handshake(sockfd, buffer, data_size);
            break;
        case 0x01:
            printf("Packet type: Ping Request\n");
            handle_ping(sockfd, buffer, data_size);
            close(sockfd); // close connection after ping packet!
            break;
        default:
            printf("Packet type: unknown\n");
            break;
    }
}

void update_json_response(const char* ip) {
    int max_players = 8;
    int online_players = 6;
    char* description_template = "Your IP is: %s";

    // Prepare the description with IP
    char description[50];
    snprintf(description, sizeof(description), description_template, ip);

    char* json = "{\"version\":{\"name\":\"1.21\",\"protocol\":767},\"players\":{\"max\":%d,\"online\":%d},\"description\":{\"text\":\"%s\"}}";

    // Static buffer to hold the generated JSON
    static char buffer[200];  // Adjust this size if needed
    int bytes_written = snprintf(buffer, sizeof(buffer), json, max_players, online_players, description);

    // Check if the resulting JSON is longer than the buffer size
    if (bytes_written >= sizeof(buffer)) {
        printf("JSON response is too long, >127 characters. Aborting...");
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


unsigned short get_port(int argc, char* argv[]) {
    const unsigned short DEFAULT_PORT = 25565;
   
    char* error_message = "Invalid CLI option. Use ./honeypot -p <port> (or --port)";

    if (argc == 1) {
        return DEFAULT_PORT;
    }

    if (argc > 3) {
        printf("%s", error_message);
        exit(EXIT_FAILURE);
    }


    if (strcmp(argv[1], "-p") != 0 && strcmp(argv[1], "--port") != 0) {
        printf("%s", error_message);
        exit(EXIT_FAILURE);
    }

    char* endptr;

    // Attempt to convert the first string
    unsigned long number = strtoul(argv[1], &endptr, 10);
    if (endptr == argv[1] || number > 65535) {
        printf("Invalid port range. (Range is from 0 to 65,535)");
        exit(EXIT_FAILURE);
    }

    return (unsigned short)number;
}


// Infinitely listening for connections.
void begin_listen(int server_sockfd) {
    // As we are a synchrounous app, it is fine to share a buffer for whole program's lifetime.
    static char buffer[1024] = {0};

    while (1) {
        int client_sockfd = get_client_socket(server_sockfd);
        const char *client_ip = get_client_ip(client_sockfd);
        log_ip(client_ip);
        update_json_response(client_ip);

        if (client_sockfd < 0) {
            perror("Failed to accept client connection");
            continue;
        }


        ssize_t received_bytes = receive_from_socket(client_sockfd, buffer, sizeof(buffer));

        // We could put this if, elif, else block into an infinite loop, so that the server still thinks
        // we are a legit server, however, we are not threaded/async so we only can handle ONE connection at a time.
        // Hence, we close the socket just after sending the MOTD/description, the only side effect is that the small
        // icon that's normally green and displays ping in ms when mouse is hovered will be gray and "loading".
        // But this is fine, since we have deceived successfully the client by grabbing its IP.
        //
        // We effectively make the CPU work only for constructing the MOTD/description and sending it.
        if (received_bytes > 0) {
            printf("Received %ld bytes\n", received_bytes);
            handle_packet(client_sockfd, buffer, received_bytes);

        } else if (received_bytes == 0) {
            printf("Connection closed by client\n");
            break;

        } else {
            printf("Error receiving data\n");
            break;
        }

        close(client_sockfd);

        printf("\n***********************************************\n\n");
    }
}


int main(int argc, char **argv) {
    unsigned short listening_port = get_port(argc, argv);
    printf("Listening on 0.0.0.0:%d...", listening_port);

    int server_sockfd = get_server_socket(listening_port);

    // This function contains an infinite loop.
    begin_listen(server_sockfd);

    close(server_sockfd);
    return 0;
}
