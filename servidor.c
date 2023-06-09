#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>

#define MAX_BUF_SIZE 1024
#define MAX_DATA_SIZE 512
#define ADLER_BASE 65521

char buffer[MAX_BUF_SIZE];
struct sockaddr_in client_address;
int next_seq_num = 0;

// estrutura de um pacote RDT
struct RDT_Packet
{
    int seq_num;              // número de sequência
    int ack;                  // flag de acuso
    char data[MAX_DATA_SIZE]; // dados
    unsigned int checksum;    // soma de verificação
    int size_packet;          // tamanho do pacote
} Packet;

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

// cria um pacote com base nos dados de entrada
struct RDT_Packet create_packet(int seq_num, int ack, char *data, int size_packet, unsigned int checksum)
{
    struct RDT_Packet packet;
    // área da memória do pacote é zerada
    memset(&packet, 0, sizeof(packet));

    // parâmetros são passados
    packet.seq_num = seq_num;
    packet.ack = ack;
    packet.size_packet = size_packet;

    // o payload é copiado para o pacote
    memcpy(packet.data, data, size_packet);

    // soma de verificação
    packet.checksum = checksum;

    return packet;
}

void generate_payload(char *message, int request_number)
{
    int i;
    for (i = 0; i < request_number; i++)
    {
        int random_payload = (rand() % request_number);
        sprintf(message, "%d", random_payload);
        memcpy(buffer, message, sizeof(message));
    }
}

void parameters(struct RDT_Packet packet)
{
    if (packet.seq_num == 0)
    {
        next_seq_num = 1;
    }
    else
    {
        next_seq_num = 0;
    }
}

// envia um pacote para um socket
int rdt_send(int socketfd, int request_number)
{
    struct timeval timeout;
    timeout.tv_sec = 1; // segundos
    timeout.tv_usec = 0; // microssegundos

    // timeout para recebimento
    if (setsockopt(socketfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        perror("setsockopt(..., SO_SNDTIMEO ,...");
        return -1;
    }
    timeout.tv_sec = 5; // segundos
    timeout.tv_usec = 0; // microssegundos
    if (setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        perror("setsockopt(..., SO_SNDTIMEO ,...");
        return -1;
    }

    int timeoutMax = 0; // variavel de controle para o maximo de timeouts tolerados pelo servidor
    int i = 0;
    int tolen = sizeof(client_address);
    int messageSent = 0, ack_receive = 0, ack_number = 0;

    while (i < request_number && timeoutMax < 8)
    { // Caso não receba o ack 8 vezes consecutivas
        timeoutMax = 0;
        i++;
        char message[MAX_BUF_SIZE] = "";
        generate_payload(message, request_number);
        unsigned int aux_checksum = adler32(buffer, strlen((char *)buffer));
        struct RDT_Packet packet = create_packet(next_seq_num, next_seq_num, buffer, strlen(buffer), aux_checksum); // cria o pacote
        do
        {
            messageSent = sendto(socketfd, &packet, sizeof(struct RDT_Packet), MSG_CONFIRM, (struct sockaddr *)&client_address, tolen);
            if (messageSent == -1)
            {
                printf("Erro ao enviar pacote. Reenviando...\n");
            }
            else
            {
                printf("\n");
                printf("Enviado pacote %d\n Conteúdo: %s\n Ack enviado: %d\n  %d timeouts\n", packet.seq_num, packet.data, packet.ack, timeoutMax);

                // buffer é zerado para receber o ack
                memset(buffer, 0, sizeof(buffer));
                ack_receive = recvfrom(socketfd, (char *)buffer, MAX_BUF_SIZE, MSG_WAITALL, (struct sockaddr *)&client_address, &tolen);
		
                if (ack_receive == -1)
                {
                    printf("O ack não foi recebido. Reenviando último pacote...\n");
                }
                else
                {
                    ack_number = atoi(buffer);
                    printf("Ack recebido: %d\n", ack_number);
                    if (ack_number != packet.seq_num)
                    {
                        printf("O ack recebido do cliente está errado. Reenviando último pacote...\n");
                    }
                }
            }
            timeoutMax++;
        } while ((messageSent == -1 || ack_receive == -1 || ack_number != packet.seq_num) && timeoutMax < 8); // SE ESTIVER ERRADO, REPITA ATÉ DAR CERTO

        parameters(packet);
    }
    return 0;
}

void rdt_recv(int socketfd)
{
    struct timeval timeout;
    timeout.tv_sec = 40; // segundos
    timeout.tv_usec = 0; // microssegundos

    // timeout para recebimento
    if (setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        perror("setsockopt(..., SO_SNDTIMEO ,...");
    }
    int tolen, n, check_loss = -1;
    tolen = sizeof(client_address);
        int request = recvfrom(socketfd, (char *)buffer, MAX_BUF_SIZE, MSG_WAITALL, (struct sockaddr *)&client_address, &tolen);
        int request_number = atoi(buffer);
        if (request != -1 && strlen(buffer) != 0)
        {
            printf("Enviando pacotes...\n");
            check_loss = rdt_send(socketfd, request_number);
        }
        else
        {
            printf("Erro: falha ao receber a requisição\n");
        }
    
}

int main(int argc, char *argv[])
{
    int socket_fd, bytes;

    socklen_t server_length;
    unsigned int sequence_number = 0;
    int ack_number;

    // Verifica se o usuário passou os argumentos corretamente
    if (argc != 2)
    {
        printf("Uso: ./server <porta>\n");
        return -1;
    }

    // Abre o socket UDP
    if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printf("Erro ao inicializar o socket\n");
        return -1;
    }

    // Configura o endereço do servidor
    // memset(&client_address, 0, sizeof(client_address));
    client_address.sin_family = AF_INET;
    client_address.sin_port = htons(atoi(argv[1]));
    // inet_aton(argv[1], client_address.sin_addr.s_addr);
    client_address.sin_addr.s_addr = INADDR_ANY;

    // Aguarda a conexão
    if (bind(socket_fd, (struct sockaddr *)&client_address, sizeof(client_address)) < 0)
    {
        perror("bind()");
        printf("Erro ao realizar o bind\n");
        return -1;
    }
    printf("Aguardando conexão...\n");

    rdt_recv(socket_fd);
    return 0;
}
