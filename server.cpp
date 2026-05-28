//Caio Eloi Campos e Kaique de Oliveira e Silva

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "protocol.h"

struct Sessao {
    struct sockaddr_in endereco;
    socklen_t tam_endereco;

    bool ativa;
    bool terminou;

    int tentativas;
    unsigned short ultima_seq;

    unsigned char ultima_resposta[MSG_SIZE_FULL];
    int tam_ultima;
    bool tem_ultima;
};

bool mesmo_cliente(struct sockaddr_in a, struct sockaddr_in b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr &&
           a.sin_port == b.sin_port;
}

bool senha_valida(std::string s) {
    if (s.size() < 4 || s.size() > 8) {
        return false;
    }

    bool visto[10];
    memset(visto, 0, sizeof(visto));

    for (int i = 0; i < (int)s.size(); i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }

        int d = s[i] - '0';

        if (visto[d]) {
            return false;
        }

        visto[d] = true;
    }

    return true;
}

std::string sorteia_senha(int n) {
    int d[10] = {0,1,2,3,4,5,6,7,8,9};

    // embaralha meio simples msm
    for (int i = 0; i < 10; i++) {
        int j = rand() % 10;
        int aux = d[i];
        d[i] = d[j];
        d[j] = aux;
    }

    std::string s;
    for (int i = 0; i < n; i++) {
        s += (char)('0' + d[i]);
    }

    return s;
}

void calcula_resposta(std::string senha, unsigned char *palpite,
                      int n, unsigned char *resp) {
    bool usou_senha[8];
    bool usou_palpite[8];

    memset(usou_senha, 0, sizeof(usou_senha));
    memset(usou_palpite, 0, sizeof(usou_palpite));

    for (int i = 0; i < n; i++) {
        resp[i] = '-';
    }

    // primeiro os que estão na posição certa
    for (int i = 0; i < n; i++) {
        if (palpite[i] == senha[i] - '0') {
            resp[i] = '*';
            usou_senha[i] = true;
            usou_palpite[i] = true;
        }
    }

    // depois os que existem, mas estão no lugar errado
    for (int i = 0; i < n; i++) {
        if (usou_palpite[i]) {
            continue;
        }

        for (int j = 0; j < n; j++) {
            if (usou_senha[j]) {
                continue;
            }

            if (palpite[i] == senha[j] - '0') {
                resp[i] = '+';
                usou_senha[j] = true;
                break;
            }
        }
    }
}

void envia(int sock, unsigned char *buf, int tam,
           struct sockaddr_in end, socklen_t tam_end) {
    sendto(sock, buf, tam, 0, (struct sockaddr*)&end, tam_end);
}

void guarda(Sessao& s, unsigned char *buf, int tam) {
    memcpy(s.ultima_resposta, buf, tam);
    s.tam_ultima = tam;
    s.tem_ultima = true;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <porta> <senha> <NT>\n", argv[0]);
        return 1;
    }

    int porta = atoi(argv[1]);
    std::string senha = argv[2];
    int NT = atoi(argv[3]);

    if (NT < 1) {
        return 1;
    }

    if (senha.size() < 4 || senha.size() > 8) {
        return 1;
    }

    bool so_zero = true;
    for (int i = 0; i < (int)senha.size(); i++) {
        if (senha[i] != '0') {
            so_zero = false;
        }
    }

    if (so_zero) {
        srand(time(NULL));
        senha = sorteia_senha(senha.size());
    } else {
        if (!senha_valida(senha)) {
            return 1;
        }
    }

    int NA = senha.size();

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));

    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(porta);

    if (bind(sock, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }

    Sessao sessoes[2];
    memset(sessoes, 0, sizeof(sessoes));

    int qtd_sessoes = 0;
    int finalizados = 0;

    while (finalizados < 2) {
        unsigned char buf[MSG_SIZE_FULL];

        struct sockaddr_in cliente;
        socklen_t tam_cliente = sizeof(cliente);

        int lidos = recvfrom(sock, buf, sizeof(buf), 0,
                             (struct sockaddr*)&cliente, &tam_cliente);

        if (lidos < MSG_SIZE_SHORT) {
            continue;
        }

        unsigned char tipo = buf[0];

        // cliente não deveria mandar RES
        if (tipo == MSG_RES) {
            continue;
        }

        int tam_msg;
        if (lidos >= MSG_SIZE_FULL) tam_msg = MSG_SIZE_FULL;
        else tam_msg = MSG_SIZE_SHORT;

        if (!verify_checksum(buf, tam_msg)) {
            continue;
        }

        int pos = -1;

        for (int i = 0; i < qtd_sessoes; i++) {
            if (sessoes[i].ativa && mesmo_cliente(sessoes[i].endereco, cliente)) {
                pos = i;
                break;
            }
        }

        Message msg;
        deserialize(buf, tam_msg, msg);

        if (tipo == MSG_HEL) {
            if (pos != -1) {
                // HEL repetido, reenvia a ultima resp
                if (sessoes[pos].tem_ultima) {
                    envia(sock, sessoes[pos].ultima_resposta,
                          sessoes[pos].tam_ultima, cliente, tam_cliente);
                }

                continue;
            }

            if (qtd_sessoes >= 2) {
                continue;
            }

            pos = qtd_sessoes;
            qtd_sessoes++;

            memset(&sessoes[pos], 0, sizeof(Sessao));

            sessoes[pos].ativa = true;
            sessoes[pos].terminou = false;
            sessoes[pos].endereco = cliente;
            sessoes[pos].tam_endereco = tam_cliente;

            unsigned char dados[8];
            memset(dados, '?', sizeof(dados));

            Message resp;
            unsigned char resp_buf[MSG_SIZE_FULL];

            make_message(resp, MSG_RES, NT, dados, NA, MSG_SIZE_FULL);
            serialize(resp, resp_buf, MSG_SIZE_FULL);

            envia(sock, resp_buf, MSG_SIZE_FULL, cliente, tam_cliente);
            guarda(sessoes[pos], resp_buf, MSG_SIZE_FULL);
        }
        else if (tipo == MSG_TRY) {
            if (pos == -1) {
                Message erro;
                unsigned char erro_buf[MSG_SIZE_SHORT];

                make_message(erro, MSG_ERR, 0, NULL, 0, MSG_SIZE_SHORT);
                serialize(erro, erro_buf, MSG_SIZE_SHORT);

                envia(sock, erro_buf, MSG_SIZE_SHORT, cliente, tam_cliente);
                continue;
            }

            Sessao& s = sessoes[pos];

            // se for retransmissao, manda a msm resposta
            if (s.tem_ultima && msg.seqnum == s.ultima_seq) {
                envia(sock, s.ultima_resposta, s.tam_ultima, cliente, tam_cliente);
                continue;
            }

            int esperado = s.tentativas + 1;

            if ((int)msg.seqnum != esperado) {
                Message erro;
                unsigned char erro_buf[MSG_SIZE_SHORT];

                make_message(erro, MSG_ERR, 0, NULL, 0, MSG_SIZE_SHORT);
                serialize(erro, erro_buf, MSG_SIZE_SHORT);

                envia(sock, erro_buf, MSG_SIZE_SHORT, cliente, tam_cliente);
                guarda(s, erro_buf, MSG_SIZE_SHORT);

                s.ultima_seq = msg.seqnum;
                continue;
            }

            if (s.tentativas >= NT) {
                Message erro;
                unsigned char erro_buf[MSG_SIZE_SHORT];

                make_message(erro, MSG_ERR, 0, NULL, 0, MSG_SIZE_SHORT);
                serialize(erro, erro_buf, MSG_SIZE_SHORT);

                envia(sock, erro_buf, MSG_SIZE_SHORT, cliente, tam_cliente);
                continue;
            }

            bool ok = true;
            bool visto[10];

            memset(visto, 0, sizeof(visto));

            for (int i = 0; i < NA; i++) {
                int d = msg.a[i];

                if (d < 0 || d > 9 || visto[d]) {
                    ok = false;
                    break;
                }

                visto[d] = true;
            }

            if (!ok) {
                // palpite ruim tbm conta uma seq enviada
                s.tentativas++;

                Message erro;
                unsigned char erro_buf[MSG_SIZE_SHORT];

                make_message(erro, MSG_ERR, msg.seqnum, NULL, 0, MSG_SIZE_SHORT);
                serialize(erro, erro_buf, MSG_SIZE_SHORT);

                envia(sock, erro_buf, MSG_SIZE_SHORT, cliente, tam_cliente);
                guarda(s, erro_buf, MSG_SIZE_SHORT);

                s.ultima_seq = msg.seqnum;
                continue;
            }

            unsigned char resposta[8];
            calcula_resposta(senha, msg.a, NA, resposta);

            s.tentativas++;

            Message resp;
            unsigned char resp_buf[MSG_SIZE_FULL];

            make_message(resp, MSG_RES, NT - s.tentativas,
                         resposta, NA, MSG_SIZE_FULL);
            serialize(resp, resp_buf, MSG_SIZE_FULL);

            envia(sock, resp_buf, MSG_SIZE_FULL, cliente, tam_cliente);
            guarda(s, resp_buf, MSG_SIZE_FULL);

            s.ultima_seq = msg.seqnum;
        }
        else if (tipo == MSG_BYE) {
            if (pos == -1) {
                continue;
            }

            Sessao& s = sessoes[pos];

            if (s.terminou) {
                envia(sock, s.ultima_resposta, s.tam_ultima, cliente, tam_cliente);
                continue;
            }

            unsigned char dados[8];
            memset(dados, 0, sizeof(dados));

            for (int i = 0; i < NA; i++) {
                dados[i] = senha[i] - '0';
            }

            Message resp;
            unsigned char resp_buf[MSG_SIZE_FULL];

            make_message(resp, MSG_RES, 0xFFFF, dados, NA, MSG_SIZE_FULL);
            serialize(resp, resp_buf, MSG_SIZE_FULL);

            envia(sock, resp_buf, MSG_SIZE_FULL, cliente, tam_cliente);
            guarda(s, resp_buf, MSG_SIZE_FULL);

            s.terminou = true;
            finalizados++;
        }
        else if (tipo == MSG_ERR) {
            continue;
        }
    }

    close(sock);
    return 0;
}
