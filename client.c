#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#define MAX_TOPIC_LEN 50
#define MAX_MSG_LEN 1500

typedef struct
{
    int sockfd;
    char topic[MAX_TOPIC_LEN + 1];
} Subscription;

typedef struct tcp_msg
{
    char topic_name[MAX_TOPIC_LEN];
    char type[16];
    char data[MAX_MSG_LEN];
} tcp_msg_t;

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    if (argc != 4)
    {
        printf("Usage: %s <ID_CLIENT> <IP_SERVER> <PORT_SERVER>\n", argv[0]);
        return 1;
    }
    int flag = 1;
    char *client_id = argv[1];
    char *server_ip = argv[2];
    int server_port = atoi(argv[3]);
    char cmd[MAX_MSG_LEN], arg1[MAX_MSG_LEN], arg2[MAX_MSG_LEN];
    Subscription subscriptions[MAX_TOPIC_LEN];
    int num_subscriptions = 0;
    char input[MAX_MSG_LEN];
    // Create TCP socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("TCP socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Disable Nagle's algorithm
    setsockopt(sockfd, IPPROTO_TCP, 1, (char *)&flag, sizeof(int));

    // Connect to the server
    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(server_ip);
    serv_addr.sin_port = htons(server_port);
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("TCP connect failed");
        exit(EXIT_FAILURE);
    }

    // Set the client ID
    char buf[MAX_TOPIC_LEN + 1 + sizeof(uint8_t)];
    strncpy(buf, client_id, MAX_TOPIC_LEN);
    buf[MAX_TOPIC_LEN] = '\0';
    if (send(sockfd, buf, sizeof(buf), 0) < 0)
    {
        perror("TCP send failed");
        exit(EXIT_FAILURE);
    }

    // Facem adresa socket-ului reutilizabila, ca sa nu primim eroare in caz ca
    // rulam de 2 ori rapid
    int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

        struct pollfd fds[2];
        int nfds = 2;

        fds[0].fd = STDIN_FILENO;
        fds[0].events = POLLIN;

        fds[1].fd = sockfd;
        fds[1].events = POLLIN;
   
    while (1)
    {
        int ready = poll(fds, nfds, -1);
        if (ready < 0)
        {
            perror("poll failed");
            exit(EXIT_FAILURE);
        }

        if (fds[0].revents & POLLIN)
        {
            // Read commands from keyboard
            fgets(input, sizeof(input), stdin);
            input[strcspn(input, "\r\n")] = '\0'; // remove newline character

            // Parse command and arguments
            if (sscanf(input, "%s %s %s", cmd, arg1, arg2) >= 2)
            {
                if (strcmp(cmd, "subscribe") == 0)
                {
                    // Handle subscribe command
                    if (num_subscriptions >= MAX_TOPIC_LEN)
                    {
                        printf("Maximum number of subscriptions reached\n");
                        continue;
                    }

                    // Check if already subscribed to the topic
                    for (int i = 0; i < num_subscriptions; i++)
                    {

                        if (strcmp(subscriptions[i].topic, arg1) == 0)
                        {
                            printf("Already subscribed to topic: %s\n", arg1);
                            break;
                        }
                    }

                    // Add subscription
                    subscriptions[num_subscriptions].sockfd = sockfd;
                    strcpy(subscriptions[num_subscriptions].topic, arg1);
                    num_subscriptions++;

                    // Send subscribe message to the server
                    char buf[MAX_TOPIC_LEN + 1 + sizeof(uint8_t) + 10]; // Add 10 bytes for the client_id
                    strncpy(buf, client_id, 10);
                    memcpy(buf + 10, cmd, MAX_TOPIC_LEN);
                    memcpy(buf + 10 + 9, arg1, MAX_TOPIC_LEN);
                    buf[10 + MAX_TOPIC_LEN] = '\0';
                    if (send(sockfd, buf, sizeof(buf), MSG_DONTWAIT) < 0)
                    {
                        perror("TCP send failed");
                        exit(EXIT_FAILURE);
                    }

                    printf("Subscribed to topic.\n");
                }
                else if (strcmp(cmd, "unsubscribe") == 0)
                {
                    // Handle unsubscribe command
                    int i;
                    int found = 0;
                    for (i = 0; i < num_subscriptions; i++)
                    {
                        if (strcmp(subscriptions[i].topic, arg1) == 0)
                        {
                            found = 1;
                            printf("Unsubscribed from topic.\n");
                            break;
                        }
                    }
                    if (found == 1)
                    {
                        // Send unsubscribe message to the server
                        char buf[MAX_TOPIC_LEN + 1 + sizeof(uint8_t) + 10];
                        strncpy(buf, client_id, 10);
                        memcpy(buf + 10, cmd, MAX_TOPIC_LEN);
                        memcpy(buf + 10 + 11, arg1, MAX_TOPIC_LEN);
                        buf[10 + MAX_TOPIC_LEN] = '\0';
                        if (send(subscriptions[i].sockfd, buf, sizeof(buf), 0) < 0)
                        {
                            perror("TCP send failed");
                            exit(EXIT_FAILURE);
                        }
                        // Remove subscription
                        subscriptions[i] = subscriptions[num_subscriptions - 1];
                        num_subscriptions--;
                    }

                    if (!found)
                    {
                        printf("Not subscribed to topic: %s\n", arg1);
                    }
                }
            }
            else if (strcmp(cmd, "exit") == 0)
            {
                char buf[15];
                strncpy(buf, client_id, 10);
                strcat(buf, "exit");
                if (send(sockfd, buf, sizeof(buf), 0) < 0)
                {
                    perror("TCP send failed");
                    exit(EXIT_FAILURE);
                }
                close(sockfd);
                break;
            }
            else
            {
                printf("Invalid command: %s\n", input);
            }
        }
        if (fds[1].revents & POLLIN)
        {
            // Receive and parse message
            tcp_msg_t received_tcp_msg;
            int num_bytes = recv(sockfd, &received_tcp_msg, sizeof(received_tcp_msg), 0);
            if (num_bytes < 0)
            {
                if (errno != EWOULDBLOCK && errno != EAGAIN)
                { 
                    perror("TCP recv failed");
                    exit(EXIT_FAILURE);
                }
            }
            else if (num_bytes == 0)
            {
                // Connection closed
                break;
            }
            else
            {
                // Process the received tcp_msg
                for (int i = 0; i < num_subscriptions; i++)
                {
                    if (strcmp(subscriptions[i].topic, received_tcp_msg.topic_name) == 0)
                    {
                        printf("%s - %s - %s\n", received_tcp_msg.topic_name, received_tcp_msg.type, received_tcp_msg.data);
                    }
                }
            }
        }
    }
    return 0;
}
