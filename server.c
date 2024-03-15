#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <math.h>
#include <poll.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#define MAX_CLIENTS 10
#define MAX_TOPIC_LENGTH 50
#define MAX_PAYLOAD_LENGTH 1500
#define MAX_NR_OF_SUBSCRIPTIONS 40
int num_fds = 2;
struct pollfd fds[MAX_CLIENTS + 2]; // +2 for the listening socket and the UDP socket

typedef struct
{
    int socket;
    char client_id[11];
    bool is_connected;
    char topic[MAX_NR_OF_SUBSCRIPTIONS][MAX_TOPIC_LENGTH + 1];
    char message[MAX_PAYLOAD_LENGTH + 1];
    char sf;
} Client;

Client clients[MAX_CLIENTS];
int tcp_socket;
int udp_socket;
int udp_port;

typedef struct udp_msg
{
    char topic_name[MAX_TOPIC_LENGTH];
    int type;
    unsigned char data[MAX_PAYLOAD_LENGTH];
} udp_msg_t;

typedef struct tcp_msg
{
    // se trimite si topic size
    char topic_name[MAX_TOPIC_LENGTH];
    char type[16];
    char data[MAX_PAYLOAD_LENGTH];
} tcp_msg_t;

double pow(double x, double y)
{
    double i;
    double ret = 1.0;
    for (i = 0; i < y; i++)
        ret = ret * x;
    return ret;
}

void initialize_clients()
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].socket = -1;
        clients[i].client_id[0] = '\0';
        clients[i].is_connected = false;
    }
}

int find_client_index_by_id(const char *client_id)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].is_connected && strcmp(clients[i].client_id, client_id) == 0)
        {
            return i;
        }
    }
    return -1;
}

bool is_client_id_taken(const char *client_id)
{
    return (find_client_index_by_id(client_id) != -1);
}

int find_client_index_by_socket(int socket)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].socket == socket)
        {
            return i;
        }
    }
    return -1;
}

void remove_client(int client_index)
{
    if (client_index >= 0 && client_index < MAX_CLIENTS && clients[client_index].is_connected)
    {
        int client_socket = clients[client_index].socket;
        char client_id[11];

        // Remove the client from the list
        clients[client_index].is_connected = false;

        // Close the client socket
        close(client_socket);
        // Remove the client socket from the pollfd array
        for (int i = 0; i < num_fds; i++)
        {
            if (fds[i].fd == client_socket)
            {
                // Shift the rest of the fds array to fill the gap
                for (int j = i; j < num_fds - 1; j++)
                {
                    fds[j].fd = fds[j + 1].fd;
                    fds[j].events = fds[j + 1].events;
                }
                num_fds--;
                break;
            }
        }
        // Announce the client disconnection
        printf("Client %s disconnected\n", client_id);
    }
}

void add_client(int socket, const char *client_id)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (!clients[i].is_connected)
        {
            clients[i].socket = socket;
            strcpy(clients[i].client_id, client_id);
            clients[i].is_connected = true;

            // Add clientSocket to the fds array
            fds[num_fds].fd = socket;
            fds[num_fds].events = POLLIN;
            num_fds++;

            break;
        }
    }
}

struct tcp_msg udp_to_tcp(unsigned char *buf)
{
    char topic[MAX_TOPIC_LENGTH + 1];
    unsigned char tip_date;
    char payload[MAX_PAYLOAD_LENGTH + 1];
    memcpy(topic, buf, MAX_TOPIC_LENGTH);
    topic[MAX_TOPIC_LENGTH] = '\0';
    // printf("Topic: %s\n", topic);
    memcpy(&tip_date, buf + MAX_TOPIC_LENGTH, sizeof(unsigned char));
    // printf("Tip date: %d\n", tip_date);
    memset(payload, 0, MAX_PAYLOAD_LENGTH);
    memcpy(payload, buf + MAX_TOPIC_LENGTH + sizeof(unsigned char), MAX_PAYLOAD_LENGTH);
    // printf("Topic: %s\n", payload);
    struct tcp_msg tcp_msg;
    if (tip_date > 3)
    {
        printf("Wrong message type.\n");
        tcp_msg.topic_name[0] = '\0';
        return tcp_msg;
    }
    memcpy(payload, buf + MAX_TOPIC_LENGTH + sizeof(unsigned char), MAX_PAYLOAD_LENGTH);
    long int_num;
    double real_num;

    strcpy(tcp_msg.topic_name, topic);

    switch (tip_date)
    {
    case 0:
        // INT
        if (payload[0] > 1)
        {
            printf("Wrong sign byte.\n");
            tcp_msg.topic_name[0] = '\0';
            return tcp_msg;
        }
        int_num = ntohl(*(uint32_t *)(payload + 1));

        if (payload[0])
        {
            int_num = -int_num;
        }

        strcpy(tcp_msg.type, "INT");
        sprintf(tcp_msg.data, "%ld", int_num);

        break;

    case 1:
        // SHORT_REAL
        real_num = ntohs(*(uint16_t *)(payload));
        real_num /= 100;
        strcpy(tcp_msg.type, "SHORT_REAL");
        sprintf(tcp_msg.data, "%.2f", real_num);
        break;

    case 2:
        // FLOAT
        if (payload[0] > 1)
        {
            printf("Wrong sign byte.\n");
            tcp_msg.topic_name[0] = '\0';
            return tcp_msg;
        }

        real_num = ntohl(*(uint32_t *)(payload + 1));
        real_num /= pow(10, payload[5]);

        if (payload[0])
        {
            real_num *= -1;
        }

        sprintf(tcp_msg.data, "%lf", real_num);
        strcpy(tcp_msg.type, "FLOAT");
        break;

    default:
        // s-a primit un STRING
        strcpy(tcp_msg.type, "STRING");
        memset(tcp_msg.data + strlen(payload), 0, 1500 - strlen(payload));
        memcpy(tcp_msg.data, payload, strlen(payload)); // era 1501 aici (plm)
        break;
    }

    return tcp_msg;
}

void handle_udp_message(tcp_msg_t tcp_msg)
{
    // Send the UDP message to all connected clients
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].is_connected)
        {
            // printf("Sending message to client %s\n", clients[i].client_id);
            send(clients[i].socket, &tcp_msg, sizeof(tcp_msg), 0);
        }
    }
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    char buffer[1024];
    initialize_clients();
    // Create TCP socket
    int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket < 0)
    {
        perror("Error creating TCP socket");
        return 1;
    }

    // Create UDP socket
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0)
    {
        perror("Error creating UDP socket");
        return 1;
    }
    int flag = 1; // nagle's algorithm
    setsockopt(tcp_socket, IPPROTO_TCP, 1, (char *)&flag, sizeof(int));

    // Bind TCP socket
    struct sockaddr_in tcp_address;
    tcp_address.sin_family = AF_INET;
    tcp_address.sin_addr.s_addr = INADDR_ANY;
    tcp_address.sin_port = htons(port);
    if (bind(tcp_socket, (struct sockaddr *)&tcp_address, sizeof(tcp_address)) < 0)
    {
        perror("Error binding TCP socket");
        return 1;
    }

    // Bind UDP socket
    struct sockaddr_in udp_address;
    udp_address.sin_family = AF_INET;
    udp_address.sin_addr.s_addr = INADDR_ANY;
    udp_address.sin_port = htons(port);
    if (bind(udp_socket, (struct sockaddr *)&udp_address, sizeof(udp_address)) < 0)
    {
        perror("Error binding UDP socket");
        return 1;
    }
    // Facem adresa socket-ului reutilizabila, ca sa nu primim eroare in caz ca
    // rulam de 2 ori rapid (direct din lab7)
    int enable = 1;
    if (setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");
    if (setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    // Listen on TCP socket
    if (listen(tcp_socket, 5) < 0)
    {
        perror("Error listening on TCP socket");
        return 1;
    }
    // Create an array of struct pollfd to keep track of the listening socket and connected clients

    fds[0].fd = tcp_socket;
    fds[0].events = POLLIN;
    fds[1].fd = udp_socket;
    fds[1].events = POLLIN;
    while (true)
    {
        int poll_result = poll(fds, num_fds, -1);

        if (poll_result == -1)
        {
            perror("Error in poll");
            break;
        }

        for (int i = 0; i < num_fds; i++)
        {
            if (fds[i].revents == 0)
            {
                continue;
            }
            if (fds[i].revents & POLLHUP)
            {
                // Handle the client disconnecting
                printf("Client disconnected, removing client\n");
                remove_client(find_client_index_by_socket(fds[i].fd));
                close(fds[i].fd);

                // Remove the struct from the fds array
                fds[i] = fds[num_fds - 1];
                num_fds--;
                continue;
            }
            if (fds[i].revents & POLLERR)
            {
                // Handle error condition
                printf("Error condition on socket, removing client\n");
                remove_client(find_client_index_by_socket(fds[i].fd));
                close(fds[i].fd);

                // Remove the struct from the fds array
                fds[i] = fds[num_fds - 1];
                num_fds--;
                continue;
            }
            if (fds[i].fd == tcp_socket)
            {
                // Accept new connection
                struct sockaddr_in clientAddress;
                socklen_t clientAddressLength = sizeof(clientAddress);
                int clientSocket = accept(tcp_socket, (struct sockaddr *)&clientAddress, &clientAddressLength);
                if (clientSocket < 0)
                {
                    perror("Error accepting TCP connection");
                    continue;
                }
                setsockopt(clientSocket, IPPROTO_TCP, 1, (char *)&flag, sizeof(int));
                // Set socket to non-blocking mode
                int flags = fcntl(clientSocket, F_GETFL, 0);
                if (flags == -1)
                {
                    perror("fcntl F_GETFL");
                    exit(EXIT_FAILURE);
                }
                flags |= O_NONBLOCK;
                if (fcntl(clientSocket, F_SETFL, flags) == -1)
                {
                    perror("fcntl F_SETFL");
                    exit(EXIT_FAILURE);
                }
                // Add clientSocket to the fds array
                fds[num_fds].fd = clientSocket;
                fds[num_fds].events = POLLIN;
                num_fds++;
            }
            else if (fds[i].fd == udp_socket)
            {
                // Receive and parse UDP message
                char buf[MAX_TOPIC_LENGTH + 1 + sizeof(unsigned char) + MAX_PAYLOAD_LENGTH];
                struct sockaddr_in udp_client_address;
                socklen_t udp_client_address_length = sizeof(udp_client_address);
                ssize_t num_bytes = recvfrom(udp_socket, buf, sizeof(buf), 0, (struct sockaddr *)&udp_client_address, &udp_client_address_length);
                if (num_bytes < 0)
                {
                    perror("UDP recvfrom failed");
                    exit(EXIT_FAILURE);
                }
                else if (num_bytes == 0)
                {
                    // Connection closed
                    break;
                }

                struct tcp_msg tcp_msg;
                tcp_msg = udp_to_tcp(buf);
                handle_udp_message(tcp_msg);
                memset(buf, 0, sizeof(buf));
            }
            else
            {
                //Also receiving the client_id so we know which client sent the message
                int client_socket = fds[i].fd;
                char client_id[11];
                char command[1024];
                int received_bytes = recv(client_socket, client_id, sizeof(client_id) - 1, 0);
                if (received_bytes < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        // No data available, not an error
                    }
                    else
                    {
                        // Real error occurred
                        perror("recv error");
                        remove_client(find_client_index_by_id(client_id));
                        exit(EXIT_FAILURE);
                    }
                }
                else if (received_bytes == 0)
                {
                    // Connection closed
                    remove_client(find_client_index_by_id(client_id));
                    break;
                }
                client_id[10] = '\0'; // Terminate the client_id string
                received_bytes = recv(client_socket, command, sizeof(command) - 1, 0);
                if (received_bytes < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        // No data available, not an error
                    }
                    else
                    {
                        // Real error occurred
                        perror("recv error");
                        exit(EXIT_FAILURE);
                    }
                }
                else if (received_bytes == 0)
                {
                    // Connection closed
                    remove_client(find_client_index_by_id(client_id));
                    break;
                }
                if (strstr(command, "exit") != NULL)
                {
                    remove_client(find_client_index_by_id(client_id));
                    break;
                }

                if (find_client_index_by_id(client_id) == -1)
                {
                    for (int j = 0; j < MAX_CLIENTS; j++)
                    {
                        if (clients[j].client_id[0] == '\0')
                        {
                            client_id[received_bytes] = '\0';
                            struct sockaddr_in client_addr;
                            socklen_t addr_len = sizeof(client_addr);
                            if (getpeername(client_socket, (struct sockaddr *)&client_addr, &addr_len) == -1)
                            {
                                perror("getpeername() error");
                                break;
                            }
                            char client_ip[INET_ADDRSTRLEN];
                            if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN) == NULL)
                            {
                                perror("inet_ntop() error");
                                break;
                            }
                            int client_port = ntohs(client_addr.sin_port);

                            printf("New client %s connected from %s:%d\n", client_id, client_ip, client_port);

                            // Add client to the list
                            add_client(client_socket, client_id);
                            break;
                        }
                    }
                }
                //  Process client commands here
                //  Implement the necessary logic for subscribe, unsubscribe, and exit commands
                //  Send appropriate messages to subscribed clients

                if (strstr(command, "subscribe") != NULL)
                {
                    char topic[100];
                    strcpy(topic, command + 9);
                    // Implement logic for subscribing to a topic
                    // Announce the subscription event
                    i = find_client_index_by_id(client_id);
                    {
                        if (clients[i].is_connected)
                        {

                            for (int j = 0; i < MAX_NR_OF_SUBSCRIPTIONS; i++)
                            {
                                if (clients[i].topic[j][0] == '\0')
                                {
                                    strcpy(clients[i].topic[j], topic);
                                    break;
                                }
                            }
                        }
                    }
                }
                if (strstr(command, "unsubscribe") != NULL)
                {
                    char topic[100];
                    strcpy(topic, command + 11);
                    //  Implement logic for unsubscribing from a topic
                    //  Announce the unsubscription event
                    i = find_client_index_by_id(client_id);
                    for (int j = 0; i < MAX_NR_OF_SUBSCRIPTIONS; i++)
                    {
                        if (strstr(clients[i].topic[j], topic) != NULL)
                        {
                            clients[i].topic[j][0] = '\0';
                            break;
                        }
                    }
                }
            }
        }
    }
    close(tcp_socket);
    close(udp_socket);

    return 0;
}