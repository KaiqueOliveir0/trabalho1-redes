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

#define TIMEOUT_SEC 1
#define MAX_RETRANS 2

int manda_e_recebe(int sock, const uint8_t *envia, int tam_envia,
                   uint8_t *recebe, int tam_recebe,
                   sockaddr_in& servidor, socklen_t tam_serv) {
    timeval tv;
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int tentativas = 0;

    while (1) {
        sendto(sock, envia, tam_envia, 0, (sockaddr*)&servidor, tam_serv);

        sockaddr_in de;
        socklen_t tam_de = sizeof(de);
        int n = recvfrom(sock, recebe, tam_recebe, 0, (sockaddr*)&de, &tam_de);

        if (n < MSG_SIZE_SHORT) {
            tentativas++;
            if (tentativas > MAX_RETRANS) {
                printf("NO RES\n");
                return -1;
            }
            continue;
        }

        int tam = MSG_SIZE_SHORT;
        if (n >= MSG_SIZE_FULL) tam = MSG_SIZE_FULL;

        if (!verify_checksum(recebe, tam)) {
            tentativas++;
            if (tentativas > MAX_RETRANS) {
                printf("NO RES\n");
                return -1;
            }
            continue;
        }

        return n;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <host> <porta>\n", argv[0]);
        return 1;
    }

    char *host = argv[1];
    int porta = atoi(argv[2]);

    hostent *h = gethostbyname(host);
    if (h == NULL) {
        fprintf(stderr, "Erro ao resolver host\n");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(porta);
    memcpy(&serv.sin_addr, h->h_addr_list[0], h->h_length);

    Message msg;
    uint8_t buf_envia[MSG_SIZE_FULL];
    uint8_t buf_rec[MSG_SIZE_FULL];

    make_message(msg, MSG_HEL, 0, NULL, 0, MSG_SIZE_SHORT);
    serialize(msg, buf_envia, MSG_SIZE_SHORT);

    int n = manda_e_recebe(sock, buf_envia, MSG_SIZE_SHORT,
                           buf_rec, MSG_SIZE_FULL, serv, sizeof(serv));
    if (n < 0) {
        close(sock);
        return 1;
    }

    int tam_msg = (n >= MSG_SIZE_FULL) ? MSG_SIZE_FULL : MSG_SIZE_SHORT;
    deserialize(buf_rec, tam_msg, msg);

    if (msg.tipo != MSG_RES) {
        printf("ERRO\n");
        close(sock);
        return 1;
    }

    int NT = msg.seqnum;
    int NA = 0;
    for (int i = 0; i < 8; i++) {
        if (msg.a[i] == '?') NA++;
        else break;
    }

    printf("NA=%d, NT=%d\n", NA, NT);

    int seq = 1;
    std::string linha;

    while (std::getline(std::cin, linha)) {
        if (!linha.empty() && linha[linha.size() - 1] == '\r') {
            linha.erase(linha.size() - 1);
        }
        if (linha.empty()) continue;

        bool ok = ((int)linha.size() == NA);
        bool usado[10];
        memset(usado, 0, sizeof(usado));

        if (ok) {
            for (int i = 0; i < NA; i++) {
                char c = linha[i];
                if (c < '0' || c > '9') {
                    ok = false;
                    break;
                }
                int d = c - '0';
                if (usado[d]) {
                    ok = false;
                    break;
                }
                usado[d] = true;
            }
        }

        uint8_t dig[8];
        memset(dig, 0, sizeof(dig));

        if (ok) {
            for (int i = 0; i < NA; i++) dig[i] = linha[i] - '0';
        } else {
            int limite = linha.size() > 8 ? 8 : (int)linha.size();
            for (int i = 0; i < limite; i++) {
                if (linha[i] >= '0' && linha[i] <= '9') dig[i] = linha[i] - '0';
                else dig[i] = 255;
            }
        }

        make_message(msg, MSG_TRY, seq, dig, NA, MSG_SIZE_FULL);
        serialize(msg, buf_envia, MSG_SIZE_FULL);

        n = manda_e_recebe(sock, buf_envia, MSG_SIZE_FULL,
                           buf_rec, MSG_SIZE_FULL, serv, sizeof(serv));
        if (n < 0) {
            close(sock);
            return 1;
        }

        tam_msg = (n >= MSG_SIZE_FULL) ? MSG_SIZE_FULL : MSG_SIZE_SHORT;
        deserialize(buf_rec, tam_msg, msg);

        if (msg.tipo == MSG_ERR) {
            if (msg.seqnum > 0) {
                printf("RETRY %d\n", (int)msg.seqnum);
                seq++;
            } else {
                printf("ERRO\n");
                close(sock);
                return 1;
            }
        } else if (msg.tipo == MSG_RES) {
            char padrao[9];
            for (int i = 0; i < NA; i++) padrao[i] = msg.a[i];
            padrao[NA] = 0;

            printf("%d(%d) %s\n", seq, (int)msg.seqnum, padrao);
            seq++;
        } else {
            printf("ERRO\n");
            close(sock);
            return 1;
        }
    }

    make_message(msg, MSG_BYE, seq - 1, NULL, 0, MSG_SIZE_SHORT);
    serialize(msg, buf_envia, MSG_SIZE_SHORT);

    n = manda_e_recebe(sock, buf_envia, MSG_SIZE_SHORT,
                       buf_rec, MSG_SIZE_FULL, serv, sizeof(serv));
    if (n < 0) {
        close(sock);
        return 1;
    }

    tam_msg = (n >= MSG_SIZE_FULL) ? MSG_SIZE_FULL : MSG_SIZE_SHORT;
    deserialize(buf_rec, tam_msg, msg);

    if (msg.tipo != MSG_RES) {
        printf("ERRO\n");
        close(sock);
        return 1;
    }

    char senha[9];
    for (int i = 0; i < NA; i++) {
        if (msg.a[i] <= 9) senha[i] = '0' + msg.a[i];
        else senha[i] = msg.a[i];
    }
    senha[NA] = 0;

    printf("Senha=%s\n", senha);

    close(sock);
    return 0;
}
