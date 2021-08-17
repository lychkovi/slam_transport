// test_msg_client.c: Программа тестирует работу протокола передачи сообщений
// TCP/IP со стороны источника сообщений.
//
// Компиляция:
//   gcc test_msg_client.c msg_conn.c msg_buf.c -o test_msg_client
//

#include <sys/time.h>    // gettimeofday()
#include <unistd.h>      // usleep()
#include <signal.h>      // signal()
#include <stdlib.h>      // atoi()
#include <string.h>      // memcpy()
#include <stdio.h>
#include "msg_conn.h"


// composeMsgCloud: Создаем тестовое сообщение с облаком точек.
// 
// Исходные данные:
//   buf   - указатель на стуктуру неинициализированного буфера
//   mtu   - максимальный размер одного пакета сообщения
//   index - порядковый номер сообщения (от начала сессии)
//   Vsize - размер поля по оси X
//   Wsize - размер поля по оси Y
//   step - шаг расстояния между точками
// 
// Возвращаемые данные:
//   записываются в поля структуры buf.
//
BOOL composeMsgCloud(MsgBuffer* buf, size_t mtu, size_t index, 
    float step, float Vsize, float Wsize)
{
    BOOL status;
    MsgHeader msg;
    struct timeval time;
    float Vij = 0;  // вспомогательные переменные для 
    float Wij = 0;  // расчета координат облака точек
    float* ptr = NULL;

    // Вычисляем количество точек облака по каждой оси
    size_t Vn = Vsize / step; // кол-во точек по одной оси
    size_t Wn = Wsize / step; // кол-во точек по другой оси

    // Определяем текущее системное время
    gettimeofday(&time, NULL);

    // Создаем заголовок сообщения
    msg.index = index;
    msg.timestampNs = time.tv_sec * 1.0e9 + time.tv_usec * 1.0e3;
    msg.type = MsgTypePointCloud;
    msg.uni.cloud.trackerState = 1;
    msg.uni.cloud.integralState = 1;
    msg.uni.cloud.translation[0] = 0;
    msg.uni.cloud.translation[1] = 0.2;
    msg.uni.cloud.translation[2] = 0;
    msg.uni.cloud.rotation[0] = 0;
    msg.uni.cloud.rotation[1] = 0;
    msg.uni.cloud.rotation[2] = 0;
    msg.uni.cloud.rotation[3] = 1;
    msg.uni.cloud.npts = Vn * Wn;
    msg.magicNumber = MSG_HEADER_MAGIC;

    // Выделяем буфер для сообщения
    status = MsgBufferInit(buf, &msg, mtu);

    if (status)
    {
        // Записываем заголовок сообщения в буфер сообщения
        memcpy(buf->data, &msg, sizeof(MsgHeader));

        // Записываем координаты облака точек в буфер сообщения
        ptr = (float*) (buf->data + sizeof(MsgHeader));
        for (size_t i = 0; i < Vn; i++)
        {
            Vij = -0.5f * Vsize + step * i;
            for (size_t j = 0; j < Wn; j++)
            {
                Wij = -0.5f * Wsize + step * j;
                *ptr++ = 0;
                *ptr++ = Vij;
                *ptr++ = Wij;
            }
        }
    }
    else
    {
        printf("Failed to init message buffer!\n");
    }
    return status;
}


// Функция для обработки сигнала Ctrl+C (для остановки приложения)
BOOL needToExit = FALSE;
void signal_handler(int signum)
{
    printf("Stopping application...\n");
    needToExit = TRUE;
}


int main(int argc, char *argv[])
{
    MsgConnConfig cfg;  // конфигурация соединения
    MsgConn conn;       // объект соединения
    MsgBuffer buf;      // буфер сообщения
    size_t index = 0;   // счетчик сообщений

    // Регистрируем функцию обработки сигнала
    signal(SIGINT, signal_handler);

    // Анализируем параметры командной строки
    if (argc < 3) 
    {
       printf("Missing command line arguments!\n");
       printf("Usage:\n");
       printf("   %s serversocket clientsocket\n", argv[0]);
       return -1;
    }

    // Инициализируем структуру конфигурации соединения
    cfg.connRole = MsgConnRoleLocalSender;
    strncpy(cfg.servername, argv[1], sizeof(cfg.servername));
    strncpy(cfg.clientname, argv[2], sizeof(cfg.clientname));
    cfg.portno = -1;
    cfg.mtu = 65536;//1460*10;      // максимальный размер одного пакета
    cfg.maxListLength = 10; // максимальная длина очереди сообщений

    // Инициализируем объект соединения
    if (!MsgConnInit(&conn, &cfg))
    {
        printf("Failed to init client connection!\n");
        return -1;
    }
    printf("Talking to socket %s from socket %s...\n", 
        cfg.servername, cfg.clientname);

    // В цикле отправляем несколько сообщений
    index = 0;
    while (index < 3000 && !needToExit)
    {
        // Делаем паузу перед отправкой нового сообщения
        usleep(330000);

        // Составляем новое сообщение
        if (composeMsgCloud(&buf, cfg.mtu, index, 0.01, 4.0, 6.0))
        {
            // Отправляем сообщение
            if (MsgConnSend(&conn, &buf))
            {
                printf("Message no. %04d was sent!\n", (int)index);
            }
            else
            {
                printf("Message no. %04d failed to send!\n", (int)index);
            }
            // Освобождаем память, выделенную под буфер сообщения
            MsgBufferFree(&buf);
        }
        else
        {
            printf("Failed to compose message no. %04d!\n", (int)index);
        }

        // Инкрементируем индекс сообщения
        index++;
    }

    // Завершаем работу приложения
    MsgConnFree(&conn);
    printf("Goodbye!\n");
    return 0;
}

