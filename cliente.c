#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>

#define MAX_DATA_SIZE 512
#define MAX_PACKET_SIZE 1024
#define ADLER_BASE 65521

char buffer[MAX_DATA_SIZE];
int next_seq_num = 0;
int pacotes_recebidos = 0;

// estrutura de um pacote RDT
struct RDT_Packet
{
    int seq_num;              // número de sequência
    int ack;                  // flag de acuso
    char data[MAX_DATA_SIZE]; // dados
    unsigned int checksum;    // soma de verificação
    int size_packet;          // tamanho do pacote
};

unsigned int adler32(unsigned char *data, size_t len)
{
    unsigned int a = 1, b = 0;

    while (len > 0)
    {
        size_t tlen = len > 5550 ? 5550 : len;
        len -= tlen;
        do
        {
            a += *data++;
            b += a;
        } while (--tlen);

        a %= ADLER_BASE;
        b %= ADLER_BASE;
    }

    return (b << 16) | a;
}

void send_ack(int socketfd, struct sockaddr_in *server_address, int ack_number)
{
    char send_buffer[MAX_DATA_SIZE] = "";
    sprintf(send_buffer, "%d", ack_number);
    int check = sendto(socketfd, (char *)send_buffer, strlen(buffer), MSG_CONFIRM, (const struct sockaddr *)server_address, sizeof(*server_address));
    if (check == -1)
    {
        perror("Falha ao enviar o ack");
    }
}

void expected_seq_num(struct RDT_Packet packet)
{
    if (packet.seq_num == 1)
    {
        next_seq_num = 0;
        printf("nextack = %d\n", next_seq_num);
    }
    else
    {
        next_seq_num = 1;
        printf("nextack = %d\n", next_seq_num);
    }
}

int rdt_recv(int socketfd, struct sockaddr_in *server_address, int num_pckt)
{
    struct timeval timeout;
    timeout.tv_sec = 10; // segundos
    timeout.tv_usec = 0; // microssegundos

    // timeout para recebimento
    if (setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        perror("setsockopt(..., SO_RCVTIMEO ,...");
        return -1;
    }
    struct RDT_Packet packet;
    int i = 0;
    int last_ack = 0;
    int messageReceived = 0;
    int timeoutMax = 0; // variavel de controle para o maximo de timeouts tolerados pelo servidor
    int tolen = sizeof(server_address);
    while (i < num_pckt && timeoutMax < 8)
    { // tolerancia de aguardo
        i++;
        timeoutMax = 0;
        do
        {
            messageReceived = recvfrom(socketfd, &packet, sizeof(packet), MSG_WAITALL, (struct sockaddr *)server_address, &tolen);
            memcpy(buffer, packet.data, sizeof(buffer));
            unsigned int expected_checksum = adler32(buffer, strlen((char *)buffer)); // payload é armazenado no buffer para realizar checksum
            if (messageReceived == -1)
            {
                send_ack(socketfd, server_address, last_ack);
                printf("Erro ao receber pacote. Aguardando reenvio...");
            }
            else
            {
                if (expected_checksum != packet.checksum)
                {
                    printf("Erro na soma de verificação. Aguardando reenvio...");
                }
                else
                {
                    printf("\n");
                    printf("Pacote recebido com sucesso\n");
                    printf("O contéudo do pacote é: %s\n", packet.data);
                    printf("O número de sequência do pacote é: %d, e o ack é: %d\n", packet.seq_num, packet.ack);

                    if (packet.seq_num != next_seq_num)
                    { // sequencia de n° do pacote está errada?
                        printf("Número de sequência errada. Aguardando reenvio..\n");
                        send_ack(socketfd, server_address, last_ack); // envia ack do último pacote certo
                    }
                    else
                    {
                        printf("Número de sequência Correta.\n");
                        send_ack(socketfd, server_address, packet.ack); // envia ack do pacote atual
                    }
                }
            }
            timeoutMax += 1;
        } while ((messageReceived == -1 || packet.seq_num != next_seq_num || adler32(buffer, strlen((char *)buffer)) != packet.checksum) && timeoutMax < 8); // enquanto o pacote estiver errado, será requisitado o reenvio
        last_ack = packet.ack;
        expected_seq_num(packet); // parâmetros para o próximo pacote
        pacotes_recebidos++;
        printf("\n");
        printf("Pacotes recebidos: %d\n", pacotes_recebidos);
    }
    return 0;
}

void rdt_send(int socketfd, struct sockaddr_in *server_address, char *request, int request_number)
{
    int check_loss = -1;

        int check = sendto(socketfd, (const char *)request, strlen(request), MSG_CONFIRM, (const struct sockaddr *)server_address, sizeof(*server_address));
        if (check != -1)
        {
            printf("Enviando requisição...\n");
            check_loss = rdt_recv(socketfd, server_address, request_number);
        }
        else
        {
            printf("Erro: Falha ao enviar requisição ao servidor");
        }
}

int main(int argc, char *argv[])
{
    int socketfd;
    struct sockaddr_in server_address;
    char request[MAX_DATA_SIZE] = "";

    if (argc != 4)
    {
        printf("Usage: ./server <ip> <port> <numéro de pacotes> \n");
        return -1;
    }

    // Criando o socket
    socketfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    // Verificando se o socket foi criado
    if (socketfd < 0)
    {
        perror("Erro na criacao do socket");
        exit(1);
    }

    // memset(&server_address, 0, sizeof(server_address));
    //  Configurando o endereco
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(atoi(argv[2])); // argv[2]
    server_address.sin_addr.s_addr = inet_addr(argv[1]);

    int num_pckt = atoi(argv[3]);
    sprintf(request, "%d", num_pckt);
    // Conectando ao servidor
    /*(if (connect(socketfd, (struct sockaddr*) &server_address, sizeof(server_address)) < 0) {
        perror("Erro na conexao");
        exit(1);
    }*/

    printf("Conectado com sucesso\n");
    rdt_send(socketfd, &server_address, request, num_pckt);
    close(socketfd);

    return 0;
}
