#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

char ip[512];

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
    FILE *file = fopen(filename, "a"); // Open for appending
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
    unsigned char response_length = (unsigned char) strlen(ip);
    unsigned char length = 1 + 1 + response_length; // packet_id (1 byte) + length byte (1 byte) + response
    unsigned char total_packet_length = 1 + length; // length byte + the actual length
    unsigned char status_response_packet[total_packet_length];

    status_response_packet[0] = length;
    status_response_packet[1] = packet_id;
    status_response_packet[2] = response_length;

    memcpy(status_response_packet + 3, ip, response_length); // put all string into packet.

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


void update_description(const char *new_ip) {
    snprintf(ip, sizeof(ip),
             "{\"version\":{\"name\":\"1.21\",\"protocol\":767},\"players\":{\"max\":8,\"online\":6},\"description\":{\"text\":\"Your ip: %s\"}}",
             new_ip);
}

const unsigned short LISTENING_PORT = 25565;

int main(void) {
    printf("Server started on port %d\n", LISTENING_PORT);
    int server_sockfd = get_server_socket(LISTENING_PORT);

    while (1) {
        int client_sockfd = get_client_socket(server_sockfd);
        const char *ip_ = get_client_ip(client_sockfd);
        log_ip(ip_);
        update_description(ip_);

        if (client_sockfd < 0) {
            perror("Failed to accept client connection");
            continue;
        }

        while (1) {
            char buffer[1024] = {0};
            ssize_t received_bytes = receive_from_socket(client_sockfd, buffer, sizeof(buffer));
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
            printf("\n---\n\n");
        }

        close(client_sockfd);
    }

    close(server_sockfd);
    return 0;
}

