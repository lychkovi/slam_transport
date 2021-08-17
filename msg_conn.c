// msg_conn.c: Реализация функций для получения и отправки сообщений
// между приложениями ISAAC и ROS.
// 
// При разработке этих функций использовалась методичка по TCP:
// https://www.linuxhowtos.org/C_C++/socket.htm  
//
// а также документация по UDP:
// https://www.gnu.org/software/libc/manual/html_node/Datagrams.html
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>      // memcpy()
#include <sys/time.h>    // gettimeofday()
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>  // sockaddr_in (для TCP сокетов)
#include <sys/un.h>      // sockaddr_un (для локальных сокетов)
#include <stddef.h>      // offsetof (для локальных сокетов)
#include <netdb.h>       // hostent for client (для TCP сокетов)

#include <assert.h>
#include "msg_conn.h"


// Функция устанавливает соединение по TCP для клиентской стороны.
BOOL MsgConnInitTcpSender(MsgConn* conn, const MsgConnConfig* cfg)
{
    conn->uni.client.sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->uni.client.sockfd < 0) 
    {
        printf("ERROR opening socket!\n");
        return FALSE;
    }
    conn->uni.client.server = gethostbyname(cfg->servername);
    if (conn->uni.client.server == NULL) 
    {
        printf("ERROR, no such host\n");
        return FALSE;
    }
    bzero((char *) &conn->uni.client.serv_addr, sizeof(struct sockaddr_in));
    conn->uni.client.serv_addr.sin_family = AF_INET;
    bcopy((char *)conn->uni.client.server->h_addr, 
         (char *)&conn->uni.client.serv_addr.sin_addr.s_addr,
         conn->uni.client.server->h_length);
    conn->uni.client.serv_addr.sin_port = htons(cfg->portno);
    int errCode = connect(conn->uni.client.sockfd,
        (struct sockaddr *) &conn->uni.client.serv_addr,
        sizeof(struct sockaddr_in)); 
    if (errCode < 0)
    {
        printf("ERROR connecting!\n");
        return FALSE;
    }
    else
        return TRUE;
}


// Функция устанавливает соединение по TCP для серверной стороны.
BOOL MsgConnInitTcpReceiver(MsgConn* conn, const MsgConnConfig* cfg)
{
    socklen_t clilen = sizeof(struct sockaddr_in);

    conn->uni.server.sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->uni.server.sockfd < 0) 
    {
        printf("ERROR opening socket!\n");
        return FALSE;
    }
    bzero((char *) &conn->uni.server.serv_addr, sizeof(struct sockaddr_in));
    conn->uni.server.serv_addr.sin_family = AF_INET;
    conn->uni.server.serv_addr.sin_addr.s_addr = INADDR_ANY;
    conn->uni.server.serv_addr.sin_port = htons(cfg->portno);
    int errCode = bind(conn->uni.server.sockfd, 
        (struct sockaddr *) &conn->uni.server.serv_addr,
        sizeof(struct sockaddr_in));
    if (errCode < 0) 
    {
        printf("ERROR on binding!\n");
        return FALSE;
    }
    printf("listening to port %d...\n", cfg->portno);
    listen(conn->uni.server.sockfd, 5);
    conn->uni.server.newsockfd = accept(
        conn->uni.server.sockfd, 
        (struct sockaddr *) &conn->uni.server.cli_addr, 
        &clilen);
    if (conn->uni.server.newsockfd < 0)
    { 
        printf("ERROR on accept!\n");
        return FALSE;
    }
    else
        return TRUE;
}


// make_named_socket: Вспомогательная функция для создания локального сокета
// в ОС Линукс в виде файла на диске.
int make_named_socket(const char *filename)
{
    struct sockaddr_un name;
    int sock;
    size_t size;

    /* Create the socket. */
    sock = socket(PF_LOCAL, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        printf("Failed to create socket!\n");
        return -1;
    }

    /* Bind a name to the socket. */
    name.sun_family = AF_LOCAL;
    strncpy (name.sun_path, filename, sizeof (name.sun_path));
    name.sun_path[sizeof (name.sun_path) - 1] = '\0';

    /* The size of the address is
     the offset of the start of the filename,
     plus its length (not including the terminating null byte).
     Alternatively you can just do:
     size = SUN_LEN (&name);
    */
    size = (offsetof (struct sockaddr_un, sun_path)
        + strlen (name.sun_path));

    if (bind (sock, (struct sockaddr *) &name, size) < 0)
    {
        printf("Unable to bind name to a socket!\n");
        return -1;
    }

    return sock;
}


// Функция устанавливает соединение для локального клиента
BOOL MsgConnInitLocalSender(MsgConn* conn, const MsgConnConfig* cfg)
{
    /* Make the socket. */
    conn->uni.clientLoc.sockfd = make_named_socket(conn->config.clientname);
    if (conn->uni.clientLoc.sockfd < 0)
    {
        printf("Failed to make named socket!\n");
        return FALSE;
    }

    /* Initialize the server socket address. */
    conn->uni.clientLoc.serv_name.sun_family = AF_LOCAL;
    strcpy(conn->uni.clientLoc.serv_name.sun_path, cfg->servername);
    conn->uni.clientLoc.serv_name_size = 
        strlen(conn->uni.clientLoc.serv_name.sun_path) + 
        sizeof(conn->uni.clientLoc.serv_name.sun_family);

    return TRUE;
}


// Функция устанавливает соединение для локального сервера
BOOL MsgConnInitLocalReceiver(MsgConn* conn, const MsgConnConfig* cfg)
{
    /* Remove the filename first, it’s ok if the call fails */
    unlink(cfg->servername);

    /* Make the socket */
    conn->uni.serverLoc.sockfd = make_named_socket(cfg->servername);
    if (conn->uni.serverLoc.sockfd < 0)
    {
        printf("Failed to make named socket!\n");
        return FALSE;
    }

    conn->uni.serverLoc.client_name_size = 
        sizeof(conn->uni.serverLoc.client_name);
    return TRUE;
}


// Функция устанавливает соединение по заданным настройкам.
BOOL MsgConnInit(MsgConn* conn, const MsgConnConfig* cfg)
{
    BOOL status = FALSE;

    // Инициализируем TCP сокет
    switch (cfg->connRole)
    {
    case MsgConnRoleTcpSender:   // Инициализируем клиентскую сторону TCP
        status = MsgConnInitTcpSender(conn, cfg);
        break;
    case MsgConnRoleTcpReceiver: // Инициализируем серверную сторону TCP
        status = MsgConnInitTcpReceiver(conn, cfg);
        break;
    case MsgConnRoleLocalSender: // Инициализируем локального клиента
        status = MsgConnInitLocalSender(conn, cfg);
        break;
    case MsgConnRoleLocalReceiver: // Инициализируем локального сервера
        status = MsgConnInitLocalReceiver(conn, cfg);
        break;
    default:
        printf("Wrong connection type!\n");
        status = FALSE;
        break;
    }

    // Выделяем память для отправки или приема пакета
    if (status)
    {
        conn->pktBuf = (unsigned char*) malloc(cfg->mtu);
        if (!conn->pktBuf)
        {
            printf("Unable to allocate packet buffer!\n");
            conn->pktBody = NULL;
            status = FALSE;
        }
        else
            conn->pktBody = conn->pktBuf + sizeof(MsgPacketHeader);
    }

    // Инициализируем остальные поля структуры соединения
    conn->config = *cfg;
    conn->list = NULL;
    conn->msgErrorCount = 0;
    return status;
}


// Функция разрывает соединение
void MsgConnFree(MsgConn* conn)
{
    // Закрываем TCP сокеты
    switch (conn->config.connRole)
    {
    case MsgConnRoleTcpSender:   // Останавливаем TCP клиент
        close(conn->uni.client.sockfd);
        break;
    case MsgConnRoleTcpReceiver: // Останавливаем TCP сервер
        close(conn->uni.server.newsockfd);
        close(conn->uni.server.sockfd);
        break;
    case MsgConnRoleLocalSender: // Останавливаем локальный клиент
        close(conn->uni.clientLoc.sockfd);
        unlink(conn->config.clientname);
        break;
    case MsgConnRoleLocalReceiver: // Останавливаем локальный сервер
        close(conn->uni.serverLoc.sockfd);
        unlink(conn->config.servername);
        break;
    default:
        printf("Wrong connection type!\n");
        assert(TRUE == FALSE);
        break;
    }

    // Освобождаем память, выделенную для буфера пакета
    free(conn->pktBuf);
    conn->pktBuf = NULL;
    conn->pktBody = NULL;

    // Освобождаем память, выделенную под список сообщений
    MsgListClear(&conn->list);
}


// Функция отправляет сообщение через TCP-сокет
BOOL MsgConnSend(MsgConn* conn, const MsgBuffer* buf)
{
    unsigned char* pchunk;  // указатель на тек. фрагмент сообщения в буфере
    size_t index;           // номер текущего фрагмента сообщения
    int cbret = 0;          // количество переданных байт пакета
    MsgPacketHeader pkt;    // заголовок текущего пакета
    size_t pktSize = 0;     // фактический размер пакета
    BOOL status = FALSE;    // результат отправки сообщения

    // Проверяем контрольный код структуры буфера сообщения
    assert(buf->magicNumber == MSG_BUFFER_MAGIC);
    assert(conn->pktBuf != NULL && conn->pktBody != NULL);

    pchunk = buf->data;
    status = TRUE;
    for (index = 0; index < buf->chunksCount && status; index++) 
    {  /* по фрагментам */
        // Инициализируем заголовок пакета
        MsgPacketHeaderInit(&pkt, buf, index);
        
        // Записываем заголовок и тело пакета в буфер пакета
        memcpy(conn->pktBuf, &pkt, sizeof(MsgPacketHeader));
        memcpy(conn->pktBody, pchunk, pkt.chunkSize);
        pchunk += pkt.chunkSize;
            
        // Вычисляем фактический размер пакета
        pktSize = pkt.chunkSize + sizeof(MsgPacketHeader);

        // Выполняем отправку пакета
        switch (conn->config.connRole)
        {
        case MsgConnRoleTcpSender: // используем TCP сокет
            cbret = write(conn->uni.client.sockfd, conn->pktBuf, pktSize);
            break;
        case MsgConnRoleLocalSender: // используем локальный сокет
            cbret = sendto(conn->uni.clientLoc.sockfd, 
                conn->pktBuf, 
                pktSize, 
                0,
                (struct sockaddr *) &conn->uni.clientLoc.serv_name, 
                conn->uni.clientLoc.serv_name_size);
            break;
        default:
            printf("Wrong connection type!\n");
            cbret = -1;
            assert(TRUE == FALSE);
            break;
        }

        // Анализируем результаты отправки пакета
        if (cbret < 0) 
        {
            printf("ERROR writing to socket!\n");
            status = FALSE;
        }
        else if (cbret != pktSize)
        {
            printf("Packet fragmentation detected!\n");
            status = FALSE;
        }
    }

    if (status == FALSE)
        conn->msgErrorCount++; // инкрементируем счетчик сбойных сообщений
    return status;
}


// Функция получает сообщение через TCP-сокет
BOOL MsgConnReceive(MsgConn* conn, MsgBuffer** pbuf)
{
    int cbret = 0;          // количество принятых байт пакета
    MsgPacketHeader* pkt;   // заголовок текущего пакета
    size_t pktSize = 0;     // фактический размер пакета
    BOOL status = FALSE;    // результат приема пакета
    MsgBuffer* buf = NULL;  // буфер текущего сообщения в списке
    BOOL msgIsReady = FALSE;// собрано полное сообщение
    MsgHeader* msg = NULL;
    size_t msg_size = 0;    // ожидаемый размер буфера сообщения

    // Проверяем контрольный код структуры буфера сообщения
    assert(pbuf != NULL);
    assert(conn->pktBuf != NULL && conn->pktBody != NULL);

    // Обнуляем буфер пакета
    bzero(conn->pktBuf, conn->config.mtu);

    // Проверяем готовность новых данных в сокете
    static struct timeval timelast;
    struct timeval timecurr;
    struct timeval timeout;
    timeout.tv_sec = 2; // время ожидания в секундах
    timeout.tv_usec = 0; // время ожидания в сек
    int sock = -1;
    fd_set socks;
    FD_ZERO(&socks);
    if (conn->config.connRole == MsgConnRoleTcpReceiver)
        sock = conn->uni.server.newsockfd;
    else
        sock = conn->uni.serverLoc.sockfd;
    FD_SET(sock, &socks);
    if (select(sock + 1, &socks, NULL, NULL, &timeout) > 0)
    {
        // Принимаем один пакет из сокета
        switch (conn->config.connRole)
        {
        case MsgConnRoleTcpReceiver: // используем TCP сокет
            //cbret = read(conn->uni.server.newsockfd,
            //    conn->pktBuf,
            //    conn->config.mtu);
            cbret = recv(sock,
    	        conn->pktBuf,
		        conn->config.mtu, 
    	        MSG_WAITALL);
            //printf("cbret = %d\n", cbret);
            gettimeofday(&timecurr, NULL);
            if (cbret > 0)
            {
                timelast = timecurr;
            }
            else if (timecurr.tv_sec - timelast.tv_sec > timeout.tv_sec)
            {
                printf("Timeout for data expired - resetting connection!\n");
                MsgConnConfig cfg = conn->config;
                MsgConnFree(conn);
                MsgConnInit(conn, &cfg);
            }
            break;
        case MsgConnRoleLocalReceiver: // используем локальный сокет
            cbret = recvfrom(sock, 
                conn->pktBuf, 
                conn->config.mtu, 
                0,
                (struct sockaddr *) &conn->uni.serverLoc.client_name, 
                &conn->uni.serverLoc.client_name_size);
            break;
        default:
            printf("Wrong connection type!\n");
            cbret = -1;
            assert(TRUE == FALSE);
            break;
        }
    }
    else
    {
        //printf("No data arrived!\n");
        cbret = 0;
    }

    // Анализируем результаты приема пакета
    status = TRUE;
    if (cbret < 0) 
    {
        printf("ERROR reading from socket!\n");
        status = FALSE;
    }
    else if (cbret == 0)
    {
        // Пока нет новых данных
        status = TRUE;
    }
    else if (cbret < sizeof(MsgPacketHeader))
    {
        printf("Corrupted packet received!\n");
        status = FALSE;
    }
    else
    {
        // Проверяем корректность заголовка пакета
        pkt = (MsgPacketHeader*) conn->pktBuf;
        if (pkt->magicNumber != MSG_PACKET_MAGIC)
        {
            printf("Corrupted packet received!\n");
            status = FALSE;
        }
        else if (cbret != pkt->chunkSize + sizeof(MsgPacketHeader))
        {
            printf("Corrupted packet received!\n");
            status = FALSE;
        }
        else
        {
            // Ищем буфер соответствующего сообщения в списке буферов
            buf = MsgListFind(conn->list, pkt->msgIndex);
            if (!buf)
            {
                // Буфер не найден - нужно создавать новый буфер
                // Проверяем условие превышения порога буферов
                if (MsgListGetLength(conn->list) > conn->config.maxListLength)
                {
                    printf("Message bufer list overrun!\n");
                    MsgListClear(&conn->list);
                }

                // Создаем новый буфер
                buf = MsgListCreate(&conn->list);
                if (!buf)
                {
                    // Буфер не готов - выходим с ошибкой
                    printf("Unable to create message buffer!\n");
                    status = FALSE;
                }
                else
                {
                    // Инициализируем созданный буфер по заголовку пакета
                    status = MsgBufferInitFromPkt(buf, pkt);
                }
            }

            if (status == TRUE)
            {
                // Буфер готов - записываем пакет в буфер
                MsgBufferPutPacket(buf, pkt, conn->pktBody);

            }
        }
    }

    // Проверяем готовность и корректность сообщения
    msgIsReady = FALSE;
    if (status == TRUE && cbret > 0)
    {
        // Проверяем, собрано ли полное сообщение
        if (MsgBufferIsFull(buf))
        {
            // Проверяем контрольный код и размер сообщения
            msg = (MsgHeader*) buf->data;
            msg_size = MsgCalcSize(msg);
            if (msg_size <= buf->size &&
                msg->magicNumber == MSG_HEADER_MAGIC)
            {
                // Сообщение корректно!
                msgIsReady = TRUE;
                *pbuf = buf;  // возвращаем указатель на буфер сообщения
            }
            else
            {
                printf("Corrupted message received!\n");
                status = FALSE;
            }
        }
    }

    // В случае проблем инкрементируем счетчик ошибок
    if (status == FALSE)
        conn->msgErrorCount++;

    // Возвращаем признак готовности сообщения
    return msgIsReady;
}


// Функция освобождает буфер сообщения и удаляет соответствующий
// узел из списка буферов (нужно после обработки принятого сообщения
// приложением). Вторым аргументом функции должен быть прямой указатель
// на буфер в списке буферов!
BOOL MsgConnBufferRelease(MsgConn* conn, MsgBuffer** pbuf)
{
    // Проверяем контрольный код структуры буфера сообщения
    assert((*pbuf)->magicNumber == MSG_BUFFER_MAGIC);

    if (MsgListDelete(&conn->list, (*pbuf)->msgIndex))
    {
        *pbuf = NULL;
        return TRUE;
    }
    else
        return FALSE;
}

