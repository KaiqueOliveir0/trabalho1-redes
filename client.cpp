//Caio Eloi Campos e Kaique de Oliveira e Silva

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

// manda um pacote e espera resposta
int manda_e_recebe(int sock, const unsigned char *envia, int tam_envia,
                   unsigned char *recebe, int tam_recebe,
                   struct sockaddr_in& serv, socklen_t tam_serv) {
    struct timeval tv;
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;

    // timeout do recv
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int tent = 0;

    while (1) {
        sendto(sock, envia, tam_envia, 0, (struct sockaddr*)&serv, tam_serv);

        struct sockaddr_in de;
        socklen_t tam_de = sizeof(de);

        int n = recvfrom(sock, recebe, tam_recebe, 0,
                         (struct sockaddr*)&de, &tam_de);

        if (n < MSG_SIZE_SHORT) {
            tent++;
            if (tent > MAX_RETRANS) {
                printf("NO RES\n");
                return -1;
            }
            continue;
        }

        int tam = MSG_SIZE_SHORT;
        if (n >= MSG_SIZE_FULL) {
            tam = MSG_SIZE_FULL;
        }

        // pacote veio, mas checksum deu ruim
        if (!verify_checksum(recebe, tam)) {
            tent++;
            if (tent > MAX_RETRANS) {
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

    struct hostent *h = gethostbyname(host);
    if (h == NULL) {
        fprintf(stderr, "Erro ao resolver host\n");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));

    serv.sin_family = AF_INET;
    serv.sin_port = htons(porta);
    memcpy(&serv.sin_addr, h->h_addr_list[0], h->h_length);

    Message msg;
    unsigned char buf_envia[MSG_SIZE_FULL];
    unsigned char buf_rec[MSG_SIZE_FULL];

    // primeiro contato com o servidor
    make_message(msg, MSG_HEL, 0, NULL, 0, MSG_SIZE_SHORT);
    serialize(msg, buf_envia, MSG_SIZE_SHORT);

    int n = manda_e_recebe(sock, buf_envia, MSG_SIZE_SHORT,
                           buf_rec, MSG_SIZE_FULL, serv, sizeof(serv));

    if (n < 0) {
        close(sock);
        return 1;
    }

    int tam_msg;
    if (n >= MSG_SIZE_FULL) tam_msg = MSG_SIZE_FULL;
    else tam_msg = MSG_SIZE_SHORT;

    deserialize(buf_rec, tam_msg, msg);

    if (msg.tipo != MSG_RES) {
        printf("ERRO\n");
        close(sock);
        return 1;
    }

    int NT = msg.seqnum;

    // NA vem pela qtd de ? na primeira resposta
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

        if (linha.empty()) {
            continue;
        }

        bool ok = ((int)linha.size() == NA);

        bool usado[10];
        memset(usado, 0, sizeof(usado));

        // valida palpite, mas se estiver errado ainda manda pro servidor
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

        unsigned char dig[8];
        memset(dig, 0, sizeof(dig));

        if (ok) {
            for (int i = 0; i < NA; i++) {
                dig[i] = linha[i] - '0';
            }
        } else {
            int limite = linha.size() > 8 ? 8 : (int)linha.size();

            for (int i = 0; i < limite; i++) {
                if (linha[i] >= '0' && linha[i] <= '9') {
                    dig[i] = linha[i] - '0';
                } else {
                    dig[i] = 255;
                }
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

        if (n >= MSG_SIZE_FULL) tam_msg = MSG_SIZE_FULL;
        else tam_msg = MSG_SIZE_SHORT;

        deserialize(buf_rec, tam_msg, msg);

        if (msg.tipo == MSG_ERR) {
            if (msg.seqnum > 0) {
                printf("RETRY %d\n", (int)msg.seqnum);
                seq++; // ERR com retry tbm conta como tentativa enviada
            } else {
                printf("ERRO\n");
                close(sock);
                return 1;
            }
        } else if (msg.tipo == MSG_RES) {
            char padrao[9];

            for (int i = 0; i < NA; i++) {
                padrao[i] = msg.a[i];
            }

            padrao[NA] = 0;

            printf("%d(%d) %s\n", seq, (int)msg.seqnum, padrao);
            seq++;
        } else {
            printf("ERRO\n");
            close(sock);
            return 1;
        }
    }

    // acabou entrada, então avisa BYE
    make_message(msg, MSG_BYE, seq - 1, NULL, 0, MSG_SIZE_SHORT);
    serialize(msg, buf_envia, MSG_SIZE_SHORT);

    n = manda_e_recebe(sock, buf_envia, MSG_SIZE_SHORT,
                       buf_rec, MSG_SIZE_FULL, serv, sizeof(serv));

    if (n < 0) {
        close(sock);
        return 1;
    }

    if (n >= MSG_SIZE_FULL) tam_msg = MSG_SIZE_FULL;
    else tam_msg = MSG_SIZE_SHORT;

    deserialize(buf_rec, tam_msg, msg);

    if (msg.tipo != MSG_RES) {
        printf("ERRO\n");
        close(sock);
        return 1;
    }

    char senha[9];

    for (int i = 0; i < NA; i++) {
        // alguns testes mandam ASCII, o servidor manda valor 0..9
        if (msg.a[i] <= 9) {
            senha[i] = '0' + msg.a[i];
        } else {
            senha[i] = msg.a[i];
        }
    }

    senha[NA] = 0;

    printf("Senha=%s\n", senha);

    close(sock);
    return 0;
}
