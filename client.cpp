// Caio Eloi
// client.cpp — Cliente UDP para o jogo de senha (Mastermind)
// Uso: ./client <host> <porta>

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include "protocol.h"

#define TIMEOUT_SEC  1
#define MAX_RETRANS  2

int send_and_receive(int sockfd, const uint8_t* send_buf, int send_size,
                     uint8_t* recv_buf, int recv_max,
                     struct sockaddr_in& srv_addr, socklen_t srv_len) {
    struct timeval tv;
    tv.tv_sec  = TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int retrans = 0;

    while (true) {
        sendto(sockfd, send_buf, send_size, 0,
               (struct sockaddr*)&srv_addr, srv_len);

        socklen_t from_len = srv_len;
        struct sockaddr_in from_addr;

        int n = recvfrom(sockfd, recv_buf, recv_max, 0,
                         (struct sockaddr*)&from_addr, &from_len);

        if (n < MSG_SIZE_SHORT) {
            retrans++;
            if (retrans > MAX_RETRANS) {
                printf("NO RES\n");
                return -1;
            }
            continue;
        }

        int check_size = (n >= MSG_SIZE_FULL) ? MSG_SIZE_FULL : MSG_SIZE_SHORT;

        if (!verify_checksum(recv_buf, check_size)) {
            retrans++;
            if (retrans > MAX_RETRANS) {
                printf("NO RES\n");
                return -1;
            }
            continue;
        }

        return n;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <host> <porta>\n", argv[0]);
        return 1;
    }

    const char* host = argv[1];
    int port = atoi(argv[2]);

    struct hostent* he = gethostbyname(host);
    if (!he) {
        fprintf(stderr, "Erro ao resolver host: %s\n", host);
        return 1;
    }

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port   = htons(port);
    memcpy(&srv_addr.sin_addr, he->h_addr_list[0], he->h_length);

    socklen_t srv_len = sizeof(srv_addr);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    Message hel_msg;
    make_message(hel_msg, MSG_HEL, 0, nullptr, 0, MSG_SIZE_SHORT);

    uint8_t hel_buf[MSG_SIZE_SHORT];
    serialize(hel_msg, hel_buf, MSG_SIZE_SHORT);

    uint8_t recv_buf[MSG_SIZE_FULL];

    int n = send_and_receive(sockfd, hel_buf, MSG_SIZE_SHORT,
                             recv_buf, MSG_SIZE_FULL,
                             srv_addr, srv_len);

    if (n < 0) {
        close(sockfd);
        return 1;
    }

    Message res_msg;
    int res_size = (n >= MSG_SIZE_FULL) ? MSG_SIZE_FULL : MSG_SIZE_SHORT;
    deserialize(recv_buf, res_size, res_msg);

    if (res_msg.tipo != MSG_RES) {
        printf("ERRO\n");
        close(sockfd);
        return 1;
    }

    int NT = (int)res_msg.seqnum;

    int NA = 0;
    for (int i = 0; i < 8; i++) {
        if (res_msg.a[i] == '?') {
            NA++;
        } else {
            break;
        }
    }

    printf("NA=%d, NT=%d\n", NA, NT);

    int seq = 1;
    std::string line;

    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            continue;
        }

        bool valid = ((int)line.size() == NA);

        if (valid) {
            bool seen[10] = {false};

            for (char c : line) {
                if (c < '0' || c > '9') {
                    valid = false;
                    break;
                }

                int d = c - '0';

                if (seen[d]) {
                    valid = false;
                    break;
                }

                seen[d] = true;
            }
        }

        uint8_t digits[8];
        memset(digits, 0, sizeof(digits));

        if (valid) {
            for (int i = 0; i < NA; i++) {
                digits[i] = line[i] - '0';
            }
        } else {
            int len = (int)line.size() > 8 ? 8 : (int)line.size();

            for (int i = 0; i < len; i++) {
                if (line[i] >= '0' && line[i] <= '9') {
                    digits[i] = line[i] - '0';
                } else {
                    digits[i] = 255;
                }
            }
        }

        Message try_msg;
        make_message(try_msg, MSG_TRY, (uint16_t)seq,
                     digits, NA, MSG_SIZE_FULL);

        uint8_t try_buf[MSG_SIZE_FULL];
        serialize(try_msg, try_buf, MSG_SIZE_FULL);

        n = send_and_receive(sockfd, try_buf, MSG_SIZE_FULL,
                             recv_buf, MSG_SIZE_FULL,
                             srv_addr, srv_len);

        if (n < 0) {
            close(sockfd);
            return 1;
        }

        res_size = (n >= MSG_SIZE_FULL) ? MSG_SIZE_FULL : MSG_SIZE_SHORT;
        deserialize(recv_buf, res_size, res_msg);

        if (res_msg.tipo == MSG_ERR) {
            uint16_t err_seq = res_msg.seqnum;

            if (err_seq > 0) {
                printf("RETRY %d\n", (int)err_seq);
                seq++;
            } else {
                printf("ERRO\n");
                close(sockfd);
                return 1;
            }
        } else if (res_msg.tipo == MSG_RES) {
            int remaining = (int)res_msg.seqnum;

            char pattern[9];
            for (int i = 0; i < NA; i++) {
                pattern[i] = (char)res_msg.a[i];
            }
            pattern[NA] = '\0';

            printf("%d(%d) %s\n", seq, remaining, pattern);
            seq++;
        } else {
            printf("ERRO\n");
            close(sockfd);
            return 1;
        }
    }

    uint16_t bye_seq = (uint16_t)(seq - 1);

    Message bye_msg;
    make_message(bye_msg, MSG_BYE, bye_seq, nullptr, 0, MSG_SIZE_SHORT);

    uint8_t bye_buf[MSG_SIZE_SHORT];
    serialize(bye_msg, bye_buf, MSG_SIZE_SHORT);

    n = send_and_receive(sockfd, bye_buf, MSG_SIZE_SHORT,
                         recv_buf, MSG_SIZE_FULL,
                         srv_addr, srv_len);

    if (n < 0) {
        close(sockfd);
        return 1;
    }

    res_size = (n >= MSG_SIZE_FULL) ? MSG_SIZE_FULL : MSG_SIZE_SHORT;
    deserialize(recv_buf, res_size, res_msg);

    if (res_msg.tipo == MSG_RES) {
        char secret_str[9];

        for (int i = 0; i < NA; i++) {
            if (res_msg.a[i] >= 0 && res_msg.a[i] <= 9) {
                secret_str[i] = '0' + res_msg.a[i];
            } else {
                secret_str[i] = (char)res_msg.a[i];
            }
        }

        secret_str[NA] = '\0';

        printf("Senha=%s\n", secret_str);
    } else {
        printf("ERRO\n");
        close(sockfd);
        return 1;
    }

    close(sockfd);
    return 0;
}