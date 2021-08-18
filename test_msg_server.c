// test_msg_server.c: Приложение тестирует работу протокола передачи сообщений
// TCP/IP со стороны приемника сообщений.
//
// Компиляция:
//   gcc test_msg_server.c msg_conn.c msg_buf.c -o test_msg_server
//

#include <unistd.h>      // usleep()
#include <signal.h>      // signal()
#include <stdlib.h>      // atoi()
#include <string.h>      // strncpy()
#include <stdio.h>
#include "msg_conn.h"


// Функция для обработки сигнала Ctrl+C (для остановки приложения)
BOOL needToExit = FALSE;
void signal_handler(int signum)
{
    printf("Stopping application...\n");
    needToExit = TRUE;
}

// Функция вычисления наименьшего из двух чисел
size_t min(size_t x, size_t y)
{
	return (x < y) ? x : y;
}


int main(int argc, char *argv[])
{
    MsgConnConfig cfg;  // конфигурация соединения
    MsgConn conn;       // объект соединения
    MsgBuffer* pbuf = NULL; // указатель на буфер сообщения
    size_t index = 0;   // счетчик сообщений

    // Регистрируем функцию обработки сигнала
    //signal(SIGINT, signal_handler);
    struct sigaction a;
    a.sa_handler = signal_handler;
    a.sa_flags = 0;
    sigemptyset( &a.sa_mask );
    sigaction( SIGINT, &a, NULL );

    // Анализируем параметры командной строки
    if (argc != 2)
    {
       printf("Missing or extra command line arguments!\n");
       printf("Usage:\n");
       printf("   %s port\n", argv[0]);
       return -1;
    }

    // Инициализируем структуру конфигурации соединения
    cfg.connRole = MsgConnRoleTcpReceiver;
    strncpy(cfg.servername, "localhost", sizeof("localhost"));
    cfg.portno = atoi(argv[1]);
    cfg.mtu = 1460*10;      // максимальный размер одного IP пакета
    cfg.maxListLength = 10; // максимальная длина очереди сообщений

    // Инициализируем объект соединения
    if (!MsgConnInit(&conn, &cfg))
    {
        printf("Failed to init server connection!\n");
        return -1;
    }

    // В цикле принимаем сообщения
    while (!needToExit)
    {
        if (MsgConnReceive(&conn, &pbuf))
        {
            printf("Message no. %04d received!\n", (int)pbuf->msgIndex);
            MsgHeader* msg = (MsgHeader*) pbuf->data;
            size_t npts = msg->uni.cloud.npts;
            float* ptr = (float*) (pbuf->data + sizeof(MsgHeader));
            ptr += 3*(npts - 6);  // переходим к концовке сообщения
            if (msg->type == MsgTypePointCloud)
            for (size_t i = 0; i < 6; i++)
            {
            	//printf("x = %12.5f, y = %12.5f, z = %12.5f\n",
            	//    ptr[0], ptr[1], ptr[2]);
            	ptr += 3;
            }
            // Удаляем обработанное сообщение из списка
            MsgConnBufferRelease(&conn, &pbuf);
            usleep(50000);
        }
    }

    // Завершаем работу приложения
    MsgConnFree(&conn);
    return 0;
}


