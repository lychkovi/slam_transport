// msg_list.h: низкоуровневый протокол для разбивки и сборки больших 
// сообщений, составленных из нескольких IP пакетов.

#define BOOL unsigned int
#define TRUE  1
#define FALSE 0

// Контрольные коды зоголовков пакета и сообщения, а также буфера
#define MSG_PACKET_MAGIC 0x55AAAA55
#define MSG_HEADER_MAGIC 0x55AA55AA
#define MSG_BUFFER_MAGIC 0xAA55AA55

/* MsgType: Перечисление задает перечень возможных типов сообщений. */
typedef enum MsgTypeEnum
{
    MsgTypePointCloud,  // сообщение с координатами точек карты
    MsgTypeImage        // сообщение с кадром от видеокамеры
} MsgType;


/* MsgImageFormat: Перечисление задает перечень форматов изображений
 * для сообщения, содержащего изображение кадра видеокамеры. */
typedef enum MsgImageFormatEnum
{
    MsgImageFormatGray, // полутоновое изображение
    MsgImageFormatRGB,  // цветное изображение RGB
    MsgImageFormatRGBA  // цветное изображение RGBA
} MsgImageFormat;


/* MsgHeader: Стуктура представляет заголовок сообщения, который
 * передается в первом пакете сообщения и идентифицирует тип
 * сообщения. */
typedef struct MsgHeaderStruct
{
    size_t index;  // порядковый номер сообщения (от начала сессии)
    double timestampNs; // временная метка для момента создания сообщения
        // в приложении ISAAC (в наносекундах от начала сессии)
    MsgType type;  // тип сообщения
    union          // вариативная часть сообщения зависит от типа
    {
        struct // Сообщение типа облако точек
        {
            // Состояние отслеживания
            size_t trackerState; // 0 - нет отслеживания, 1 - есть
            size_t integralState;// состояние интегратора
            // Положение камеры в пространстве
            double translation[3];     // координаты центра камеры
            double rotation[4];        // ее ориентация в форме кватерниона
            // Облако точек карты
            size_t npts;         // количество точек в облаке
        } cloud;
        struct // Сообщение типа кадр видеокамеры
        {
            // Параметры изображения
            MsgImageFormat format;     // формат пикселя изображения
            size_t width;     // ширина кадра в пикселях
            size_t height;    // высота кадра в пикселях
        } image;
    } uni;
    size_t magicNumber; // должно быть равно 0x55AA55AA
} MsgHeader, *MsgHeaderPtr;


// Функция вычисляет размер сообщения по данным его заголовка
extern size_t MsgCalcSize(const MsgHeader* msg);


/* MsgPacketHeader: Структура представляет заголовок отдельного пакета,
 * в таких пакетах будут передаваться фрагменты сообщения. */
typedef struct MsgPacketHeaderStruct
{
    size_t msgIndex;      // порядковый номер сообщения от начала сессии
    size_t msgSize;       // размер (заголовок+тело) сообщения в байтах
    size_t msgChunksCount;// из скольких фрагментов составлено сообщение
    size_t chunkIndex;    // порядковый номер фрагмента от начала сообщения
    size_t chunkSize;     // размер фрагмента сообщения в байтах 
        /* Все фрагменты, кроме последнего, должны иметь одинаковый размер,
         * равный максимальному размеру chunkSizeMax. */
    size_t chunkSizeMax;  // максимальный размер фрагмента
        /* Значение chunkSizeMax меньше mtu на размер заголовка пакета. */
    size_t magicNumber;   // должно быть равно 0x55AAAA55
} MsgPacketHeader, *MsgPacketHeaderPtr;


/* MsgBuffer: Структура представляет сведения и указатель на буфер
 * для сохранения сообщения в памяти. Буфер не содержит заголовки
 * пакетов, а только заголовок сообщения и тело сообщения! */
typedef struct MsgBufferStruct
{
    size_t msgIndex;     // порядковый номер сообщения от начала сессии
    size_t size;         // размер (заголовок+тело) сообщения в байтах
    size_t chunksCount;  // из скольких фрагментов составлено сообщение
    size_t chunkSizeMax; // максимальный размер фрагмента сообщения
    unsigned char* status;// указатель на массив состояний всех фрагментов 
        // сообщения: состояние 0 - пока не принят, состояние 1 - уже принят
    unsigned char* data; // указатель на начало буфера сообщения
    size_t magicNumber;  // должно быть равно 0xAA55AA55
} MsgBuffer, *MsgBufferPtr;


// Функция инициализирует структуру буфера сообщения по структуре заголовка
// самого сообщения (удобно для отправляющей стороны).
extern BOOL MsgBufferInit(MsgBuffer* buf, const MsgHeader* msg, size_t mtu);

// Функция инициализирует структуру буфера сообщения по структуре заголовка
// отдельного пакета из этого сообщения (удобно для принимающей стороны).
extern BOOL MsgBufferInitFromPkt(MsgBuffer* buf, const MsgPacketHeader* pkt);

// Функция освобождает память, выделенную для буфера сообщения и для
// массива состояний пакета сообщения.
extern void MsgBufferFree(MsgBuffer* buf);

// Функция записывает в буфер сообщения фрагмент сообщения из полученного 
// пакета.
extern void MsgBufferPutPacket(MsgBuffer* buf, const MsgPacketHeader* pkt, 
    const unsigned char* chunkDataPtr);

// Функция проверяет, все ли пакеты сообщения были записаны в буфер.
extern BOOL MsgBufferIsFull(const MsgBuffer* buf);


// Функция инициализирует заголовок пакета по его индексу и данным из 
// структуры буфера сообщения.
extern void MsgPacketHeaderInit(MsgPacketHeader* pkt, 
    const MsgBuffer* buf, size_t index);


/* MsgList: Структура представляет очередь сообщений в виде односвязного
 * списка. */
typedef struct MsgListStruct
{
    MsgBuffer buf;   // буфер памяти
    struct MsgListStruct* next;  // указатель на следующий элемент списка
} MsgList, *MsgListPtr;


// Функция отыскивает в списке узел с заданным идентификатором и 
// возвращает указатель на его тело. Если узел не найден, то функция
// возращает нулевой указатель.
extern MsgBuffer* MsgListFind(MsgList* list, size_t id);

// Функция создает новый узел, добавляет его в начало списка и 
// возвращает указатель на его тело.
extern MsgBuffer* MsgListCreate(MsgList** plist);

// Функция удаляет из списка узел с заданным идентификатором.
// Внимание! Функция освобождает память, выделенную под 
// массив состояний пакетов и под тело сообщения. 
extern BOOL MsgListDelete(MsgList** plist, size_t id);

// Функция удаляет из списка все узлы и особождает память, выделенную
// под них.
extern void MsgListClear(MsgList** plist);

// Функция возвращает текущую длину списка.
extern size_t MsgListGetLength(const MsgList* list);


