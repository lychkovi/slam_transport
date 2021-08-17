// msg_buf.c: Реализация функций разбивки и сборки больших 
// сообщений, составленных из нескольких IP пакетов

#include <stdlib.h>      // malloc(), free()
#include <string.h>      // memcpy()
#include <assert.h>
#include "msg_buf.h"


// Функция вычисляет размер буфера сообщения по данным заголовка сообщения
size_t MsgCalcSize(const MsgHeader* msg)
{
    size_t nelems = 0;     // количество точек облака или пикселей кадра
    size_t elem_size = 0;
    size_t buf_size = 0;   // размер буфера сообщения

    // Проверяем контрольный код структуры заголовка сообщения
    assert(msg->magicNumber == MSG_HEADER_MAGIC);

    // Вычисляем размер буфера сообщения в байтах
    switch (msg->type)
    {
    case MsgTypePointCloud:
        nelems = msg->uni.cloud.npts; // количество точек
        elem_size = 12; // 3 координаты типа float каждая
        break;
    case MsgTypeImage:
        nelems = msg->uni.image.width * msg->uni.image.height;
        switch (msg->uni.image.format)
        {
        case MsgImageFormatGray:
            elem_size = 1;
            break;
        case MsgImageFormatRGB:
            elem_size = 3;
            break;
        case MsgImageFormatRGBA:
            elem_size = 4;
            break;
        default:
            // Недопустимый формат пикселя в сообщении!
            assert(TRUE == FALSE);
            elem_size = 0;
            break;
        }
        break;
    default:
        // Недопустимое значение типа сообщения!
        assert(TRUE == FALSE);
        nelems = 0;
        elem_size = 0;
        break;
    }
    buf_size = nelems * elem_size + sizeof(MsgHeader);
    return buf_size;
}


// Функция инициализирует структуру буфера сообщения по структуре заголовка
// отдельного пакета из этого сообщения (удобно для принимающей стороны).
BOOL MsgBufferInitFromPkt(MsgBuffer* buf, const MsgPacketHeader* pkt)
{
    // Проверяем контрольный код структуры заголовка пакета
    assert(pkt->magicNumber == MSG_PACKET_MAGIC);

    // Инициализируем все поля структуры буфера
    buf->msgIndex = pkt->msgIndex;
    buf->size = pkt->msgSize;
    buf->chunksCount = pkt->msgChunksCount;
    buf->chunkSizeMax = pkt->chunkSizeMax;
    buf->status = (unsigned char*) malloc(pkt->msgChunksCount);
    buf->data = (unsigned char*) malloc(pkt->msgSize);
    bzero(buf->status, pkt->msgChunksCount);

    // Формируем контрольный код структуры буфера
    if (buf->status && buf->data)
    {
        buf->magicNumber = MSG_BUFFER_MAGIC;
        return TRUE;
    }
    else
    {
        buf->magicNumber = -1;
        return FALSE;
    }
}


// Функция инициализирует структуру буфера сообщения по структуре заголовка
// самого сообщения (удобно для отправляющей стороны).
BOOL MsgBufferInit(MsgBuffer* buf, const MsgHeader* msg, size_t mtu)
{
    size_t nchunks = 0;    // на сколько фрагментов будет нарезано сообщение
    size_t chunk_size = 0;
    size_t buf_size = 0;   // размер буфера сообщения
    size_t msg_size = 0;   // размер самого сообщения

    // Проверяем контрольный код структуры заголовка сообщения
    assert(msg->magicNumber == MSG_HEADER_MAGIC);
    
    msg_size = MsgCalcSize(msg);
    chunk_size = mtu - sizeof(MsgPacketHeader);
    if (msg_size % chunk_size == 0)
        nchunks = msg_size / chunk_size;
    else
        nchunks = msg_size / chunk_size + 1;
    buf_size = nchunks * chunk_size;

    // Инициализируем все поля структуры буфера
    buf->msgIndex = msg->index;
    buf->size = buf_size;
    buf->chunksCount = nchunks;
    buf->chunkSizeMax = chunk_size;
    buf->status = (unsigned char*) malloc(nchunks);
    buf->data = (unsigned char*) malloc(buf_size);
    
    // Формируем контрольный код структуры буфера
    if (buf->status && buf->data)
    {
        buf->magicNumber = MSG_BUFFER_MAGIC;
        return TRUE;
    }
    else
    {
        buf->magicNumber = -1;
        return FALSE;
    }
}


// Функция освобождает память, выделенную для буфера сообщения и для
// массива состояний пакета сообщения.
void MsgBufferFree(MsgBuffer* buf)
{
    // Проверяем контрольный код структуры буфера сообщения
    assert(buf->magicNumber == MSG_BUFFER_MAGIC);

    free(buf->status);  buf->status = NULL;
    free(buf->data);    buf->data = NULL;
    buf->msgIndex = -1;
    buf->size = 0;
    buf->chunksCount = 0;
    buf->chunkSizeMax = 0;
    
    buf->magicNumber = -1;
}


// Функция записывает в буфер сообщения фрагмент сообщения из полученного 
// пакета.
void MsgBufferPutPacket(MsgBuffer* buf, const MsgPacketHeader* pkt, 
    const unsigned char* chunkDataPtr)
{   
    unsigned char* pointer = NULL;
    size_t offsetBegin = 0; // смещение начала фрагмента от начала буфера
    size_t offsetEnd = 0;   // смещение конца фрагмента от начала буфера

    // Проверяем контрольный код структуры заголовка пакета
    assert(pkt->magicNumber == MSG_PACKET_MAGIC);
    // Проверяем контрольный код структуры буфера сообщения
    assert(buf->magicNumber == MSG_BUFFER_MAGIC);
    
    // Вычисляем адрес начала нужного фрагмента сообщения в буфере
    offsetBegin = buf->chunkSizeMax * pkt->chunkIndex;
    offsetEnd = offsetBegin + pkt->chunkSize - 1;
    pointer = buf->data + offsetBegin;

    // Проверяем условия выхода за границы буфера
    assert(pkt->chunkIndex < buf->chunksCount);
    assert(offsetBegin < buf->size && offsetEnd < buf->size);

    // Выполняем копирование данных
    memcpy(pointer, chunkDataPtr, pkt->chunkSize);

    // Отмечаем в списке статусов фрагментов, что фрагмент принят
    buf->status[pkt->chunkIndex] = 1;
}


// Функция проверяет, все ли пакеты сообщения были записаны в буфер.
BOOL MsgBufferIsFull(const MsgBuffer* buf)
{
    BOOL result = TRUE;

    // Проверяем контрольный код структуры буфера сообщения
    assert(buf->magicNumber == MSG_BUFFER_MAGIC);

    for (size_t i = 0; i < buf->chunksCount; i++)
        if (buf->status[i] == 0) result = FALSE;

    return result;
}


// Функция инициализирует заголовок пакета по его индексу и данным из 
// структуры буфера сообщения.
void MsgPacketHeaderInit(MsgPacketHeader* pkt, 
    const MsgBuffer* buf, size_t index)
{
    size_t chunk_size = 0;

    // Проверяем контрольный код структуры буфера сообщения
    assert(buf->magicNumber == MSG_BUFFER_MAGIC);
    assert(index < buf->chunksCount);

    pkt->msgIndex = buf->msgIndex;      
    pkt->msgSize = buf->size;       
    pkt->msgChunksCount = buf->chunksCount;
    pkt->chunkIndex = index;
    //if (index == buf->size / buf->chunkSizeMax)
    //    pkt->chunkSize = buf->size % buf->chunkSizeMax;
    //else
        pkt->chunkSize = buf->chunkSizeMax;
    pkt->chunkSizeMax = buf->chunkSizeMax;  
    pkt->magicNumber = MSG_PACKET_MAGIC;
}


// ----------------- Функции для работы со списком сообщений ----------------


// Функция отыскивает в списке узел с заданным идентификатором и 
// возвращает указатель на его буфер. Если узел не найден, то функция
// возращает нулевой указатель.
MsgBuffer* MsgListFind(MsgList* list, size_t id)
{
    BOOL found = FALSE;
    MsgList* node = list;
    while (!found && node != NULL)
    {
        if (node->buf.msgIndex == id)
            found = TRUE;
        else
            node = node->next;
    }
    return found ? &node->buf : NULL;
}


// Функция создает новый узел, добавляет его в начало списка и 
// возвращает указатель на его тело.
MsgBuffer* MsgListCreate(MsgList** plist)
{
    MsgList* node = (MsgList*) malloc(sizeof(MsgList));
    if (node)
    {
        node->next = *plist;
        *plist = node;
    }
    return &node->buf;
}


// Функция удаляет из списка узел с заданным идентификатором.
// Внимание! Функция освобождает память, выделенную под 
// массив состояний пакетов и под тело сообщения. 
BOOL MsgListDelete(MsgList** plist, size_t id)
{
    MsgList* head = *plist; // первый элемент в списке
    MsgList* parent = NULL; // элемент, стоящий перед удаляемым
    MsgList* target = NULL; // удаляемый элемент
    BOOL found = FALSE;     // признак обнаружения элемента в списке

    if (head == NULL)
    {
        // Список пустой - ничего не делаем
        found = FALSE;
    }
    else if (head->buf.msgIndex == id)
    {
        // Удаляем самый первый элемент в списке
        found = TRUE;
        *plist = head->next;
        MsgBufferFree(&head->buf);
        free(head);  
        head = NULL;
    }
    else
    {
        // Ищем внутри списка элемент, стоящий перед удаляемым
        parent = head; // элемент перед удаляемым
        found = FALSE;
        while (!found && parent->next != NULL)
        {
            if (parent->next->buf.msgIndex == id)
                found = TRUE;
            else
                parent = parent->next;
        }
        if (found)
        {
            // Исключаем удаляемый элемент из списка
            target = parent->next;
            parent->next = target->next;
            // Освобождаем память, выделенную под удаляемый элемент
            MsgBufferFree(&target->buf);
            free(target);  
            target = NULL;
        }
    }
    return found;
}


// Функция удаляет из списка все узлы и особождает память, выделенную
// под них.
void MsgListClear(MsgList** plist)
{
    while (*plist)
    {
        MsgListDelete(plist, (*plist)->buf.msgIndex);
    }
}


// Функция возвращает текущую длину списка.
size_t MsgListGetLength(const MsgList* list)
{
    const MsgList* node = list;
    size_t count = 0;
    while (node)
    {
        count++;
        node = node->next;
    }
    return count;
}


