// msg_conn.h: Функции и структуры для получения и отправки сообщений
// между приложениями ISAAC и ROS.


#include <netinet/in.h>  // sockaddr_in (для TCP сокетов)
#include <sys/un.h>      // sockaddr_un (для локальных сокетов)
#include "msg_buf.h"   // работа со списками пакетов сообщения


/* MsgConnType: Перечисление задает перечень сторон соединения по сокету. */
typedef enum MsgConnRoleEnum
{
    MsgConnRoleTcpSender,   // отправитель сообщений по TCP сокету
    MsgConnRoleTcpReceiver, // получатель сообщений по TCP сокету
    MsgConnRoleLocalSender, // отправитель сообщений через локальный сокет
    MsgConnRoleLocalReceiver// получатель сообщений через локальный сокет
} MsgConnRole;


/* MsgConnConfig: Системные настройки соединения. */ 
typedef struct MsgConnConfigStruct
{
    MsgConnRole connRole;// тип соединения со стороны приложения
    char servername[80]; // доменное имя сервера (для отправки сообщений)
    char clientname[80]; // доменное имя клиента
    int portno;          // номер TCP порта
    size_t mtu;          // максимальный размер IP пакета
    size_t maxListLength;// предельно допустимая длина списка буферов
        /* В случае превышения этой длины происходит принудительная
         * очистка списка буферов во избежании переполнения памяти.
         * Используется только для приема сообщений. */
} MsgConnConfig, *MsgConnConfigPtr;


/* MsgConn: Структура представляет объект соединения через TCP-сокет */
typedef struct MsgConnStruct
{
    MsgConnConfig config;// исходные настройки соединения
    MsgList* list;       // список буферов принимаемых сообщений 
                         // (всегда = NULL для отправителя)
    unsigned char* pktBuf; // буфер пакета размером config.mtu байт
    unsigned char* pktBody;// указатель на тело пакета в буфере пакета
    size_t msgErrorCount;// количество сбойных сообщений

    // Состояние текущего TCP соединения
    union 
    {
        struct 
        {
            // Для TCP отправителя (это клиент):
            int sockfd;        // сокет для соединения с сервером
            struct sockaddr_in serv_addr; // IP адрес сервера
            struct hostent *server; // параметры сервера
        } client;
        struct
        {
            // Для TCP получателя (это сервер):
            int sockfd;        // сокет для обнаружения входящих подключений
            struct sockaddr_in serv_addr; // IP адрес сервера
            int newsockfd;     // сокет для нового подключенного клиента
            struct sockaddr_in cli_addr; // IP адрес клиента
        } server;
        struct 
        {
            // Для локального отправителя (это клиент)
            int sockfd;        // сокет для исходящих подключений
            struct sockaddr_un serv_name; // имя сервера
            size_t serv_name_size; // длина имени сервера
        } clientLoc;
        struct
        {
            // Для локального получателя (это сервер)
            int sockfd;        // сокет для входящих подключений
            struct sockaddr_un client_name; // имя клиента
            socklen_t client_name_size; // длина имени клиента
        } serverLoc;
    } uni;

} MsgConn, *MsgConnPtr;


// Функция устанавливает соединение по заданным настройкам.
extern BOOL MsgConnInit(MsgConn* conn, const MsgConnConfig* cfg);

// Функция разрывает соединение
extern void MsgConnFree(MsgConn* conn);

// Функция отправляет сообщение через TCP-сокет
extern BOOL MsgConnSend(MsgConn* conn, const MsgBuffer* buf);

// Функция получает сообщение через TCP-сокет
extern BOOL MsgConnReceive(MsgConn* conn, MsgBuffer** pbuf);

// Функция освобождает буфер сообщения и удаляет соответствующий
// узел из списка буферов (нужно после обработки принятого сообщения
// приложением). Вторым аргументом функции должен быть прямой указатель
// на буфер в списке буферов!
extern BOOL MsgConnBufferRelease(MsgConn* conn, MsgBuffer** pbuf);


