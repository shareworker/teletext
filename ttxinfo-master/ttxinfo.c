#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>

static uint16_t read_be16(const uint8_t *bytes) {  //阅读两个字节
    return (bytes[0] << 8) | bytes[1];
}

#define TS_PACKET_SIZE 188
#define TS_SYNC_BYTE 0x47

/* the following is valid only for packets with a 4-byte header. fortunately, we
 * discard all the ones that don't have such a header in parse_ts_packet. */
#define TS_PACKET_DATA_SIZE 184

#define TTX_PRINTF(x, ...) do { printf("TTX : " x, __VA_ARGS__); } while(0)
#define TTX_PRINT(x) do { printf("TTX : " x); } while(0)

#define TTX_EBU_DATA_ID_MIN 0x10
#define TTX_EBU_DATA_ID_MAX 0x1F
#define TTX_EBU_DATA_UNIT_SIZE 46
#define TTX_EBU_DATA_FIELD_SIZE 44
static void parse_ttx(const uint8_t *ttx, size_t size)
{
    TTX_PRINTF("Got teletext packet of size %zu\n", size);
    if(ttx[0] < TTX_EBU_DATA_ID_MIN || ttx[0] > TTX_EBU_DATA_ID_MAX)   //数据标志符，必须等于0x10-0x1f
    {
        TTX_PRINTF("Data identifier 0x%hhx is not EBU Teletext.\n", ttx[0]);
        return;
    }
    const unsigned int data_unit_num = (size - 1) / TTX_EBU_DATA_UNIT_SIZE;
    for(unsigned int i = 0 ; i < data_unit_num ; ++i)
    {
        const uint8_t *ttx_data_unit = ttx + 1 + (i * TTX_EBU_DATA_UNIT_SIZE);
        TTX_PRINTF("data_unit_id = 0x%hhx, field_parity = %u, line_offset = %u"
            ", framing_code = 0x%hhx, magazine_and_packet_address = 0x%hhx%hhx"
            "\n",
            ttx_data_unit[0],  //data_unit_id
            ttx_data_unit[2] & 0x20,
            ttx_data_unit[2] & 0x1f,
            ttx_data_unit[3],   //第四个字节是成帧码
            ttx_data_unit[4], ttx_data_unit[5]
        );
    }
}

#undef TTX_EBU_DATA_FIELD_SIZE
#undef TTX_EBU_DATA_UNIT_SIZE
#undef TTX_EBU_DATA_ID_MIN
#undef TTX_EBU_DATA_ID_MAX
#undef TTX_PRINT
#undef TTX_PRINTF

typedef void (*pes_ready_fn)(const uint8_t *pes, size_t size, void *user_data);

#define TTX_PES_PRINTF(x, ...) \
do \
{ \
    printf("TTX_PES[%u] : " x, *ttx_pes_pkt_no - 1, __VA_ARGS__); \
} while(0)

#define TTX_PES_PRINT(x) \
do \
{ \
    printf("TTX_PES[%u] : " x, *ttx_pes_pkt_no - 1); \
} while(0)

#define PES_PRIVATE_STREAM_1_ID 0xBD
#define PES_TELETEXT_HEADER_LENGTH 0x24

static void parse_ttx_pes(const uint8_t *pes, size_t size, void *user_data)
{
    unsigned int *ttx_pes_pkt_no = user_data;
    ++(*ttx_pes_pkt_no);
    
    TTX_PES_PRINTF("Received PES packet of size %zu.\n", size); //输出size_t类型，应为unsigned int类型
    /* the PES header contains a lot of data that we really don't need. however,
     * EN 300 472 specifies some values that should appear in PES headers
     * containing teletext data, so let's check them at least. */
    
    if(pes[0] != 0x00 || pes[1] != 0x00 || pes[2] != 0x01) //前三个字节是包头起始字段
    {
        TTX_PES_PRINTF("PES magic number incorrect : %hhx%hhx%hhx.\n",  //%hhu代表unsigned short短整型，一个字节，%hu代表unsigned short int整型，两个字节
            pes[0], pes[1], pes[2]);
        return;
    }
    if(pes[3] != PES_PRIVATE_STREAM_1_ID) //第四个字节进行数据流的识别，Teletext的PID为0xbd
    {
        TTX_PES_PRINTF("PES stream ID incorrect : %hhx.\n", pes[3]);
        return;
    }
    
    uint16_t pes_pkt_len = read_be16(pes + 4); //第五六个字节是PES包长度
    if(pes_pkt_len != size - 6) /* pes_pkt_len doesn't include first 6 bytes. */
    {
        TTX_PES_PRINTF("PES packet length mismatch : %hu.\n", pes_pkt_len);
        return;
    }
    if(((pes_pkt_len + 6) % TS_PACKET_DATA_SIZE) != 0)
    {
        TTX_PES_PRINT("Declared PES packet length incorrect.\n");
        return;
    }
    /* 第七八个字节是PES包头识别标志，分别是2bit的PES加扰控制，1bit的PES优先，1bit的数据定位指示符，1bit指示版权，1bit指示原版或拷贝
    2bit的PTS/DTS标志，1bit的ESCR标志，1bit的基本流速率标志，1bit的DSM特技方式标志，1bit的附加的信息拷贝，1bit的PES CRC标志，1bit的PES扩展标志 */
    if(!(pes[6] & 0x04)) 
    {
        TTX_PES_PRINT("Data alignment indicator set to zero.\n");
        return;
    }
    
    if(pes[8] != PES_TELETEXT_HEADER_LENGTH) //第九个字节指的是PES包头长
    {
        TTX_PES_PRINTF("Header length %hhu different from expected (%hhu).\n",
            pes[8], PES_TELETEXT_HEADER_LENGTH);
        return;
    }
    
    /* there are 9 bytes preceding the PES header length field. the value of the
     * field itself refers to the number of header bytes following it. adding
     * these two numbers together, we jump straight to the data. */
    parse_ttx(pes + 9 + PES_TELETEXT_HEADER_LENGTH,
        size - 9 - PES_TELETEXT_HEADER_LENGTH);
    
    TTX_PES_PRINT("Finished.\n");
}

#undef PES_PRIVATE_STREAM_1_ID
#undef PES_TELETEXT_HEADER_LENGTH
#undef TTX_PES_PRINT
#undef TTX_PES_PRINTF

/* parse one packet pointed to by 'tspkt', and return a pointer to the first
 * data byte inside the packet. if the TS packet is in any way invalid, or its
 * PID does not match 'pid', return NULL. 'pusi' is set to the value of the
 * payload unit start indicator in the header. */
static const uint8_t *parse_ts_packet(const uint8_t *tspkt, uint16_t pid,
    unsigned int *pusi)
{
    uint16_t this_pid;
    if(tspkt[0] != TS_SYNC_BYTE) return NULL; //取出同步字节
    if((tspkt[1] & 0x80)) return NULL; /* transport_error_indicator */
    *pusi = (tspkt[1] & 0x40); /* payload_unit_start_indicator */
    
    this_pid = (((tspkt[1] & 0x1f) << 8) | tspkt[2]);
    if(this_pid != pid) return NULL;
    
    if((tspkt[3] & 0xc0)) return NULL; /* transport_scrambling_indicator 加密标志位，00代表未加密*/ 
    
    /* teletext packets must have adaptation_field_control equal to 01
     * (only payload) or 10 (only adaptation). the adaptation field is of no
     * interest to us. Teletext数据包必须有等于01（代表只有负载）或者10（只有自适应域）的自适应域控制段，自适应域对我们处理teletext没有用处*/
    if(((tspkt[3] & 0x30) >> 4) != 1) return NULL;
    
    /* ignore the continuity counter. */
    return tspkt + 4;
}

typedef struct pes_assemble_ctx_
{
    uint8_t *buf;  //元素以字节为单位
    size_t size;
    size_t offset;
    unsigned int in_pes;
} pes_assemble_ctx;

static void assemble_pes_from_ts(pes_assemble_ctx *ctx, const uint8_t *ts_data,
    uint16_t pid, pes_ready_fn pes_ready, void *pes_ready_user_data)
{
    unsigned int pusi; //有效负载开始标志
    const uint8_t *pes_data = parse_ts_packet(ts_data, pid, &pusi);  //解析TS包，地址指向有效负载
    if(pes_data)
    {
        if(!ctx->in_pes && !pusi)
        {
            /* this will happen only when starting to read the TS and means that
             * we got some TS packets that were a part of a PES packet whose
             * beginning is not in our stream. 这只会发生在刚开始读取TS码流的时候，且意味着虽然获得了TS码流，但只是PES包的部分，并不完整*/
            return;
        }
        else if(!ctx->in_pes && pusi)
        {
            /* a new packet begins. write to the beginning of the buffer. no
             * bounds check needed, since ctx->buf is guaranteed to be at least
             * 2048 bytes. 新数据包开始，写入缓冲区，不需要边界检查，因为缓存区至少为2048个字节*/
            ctx->in_pes = 1; //代表是一个PES包
            memcpy(ctx->buf, pes_data, TS_PACKET_DATA_SIZE); //读入184个字节TS负载数据
            ctx->offset = TS_PACKET_DATA_SIZE; //地址偏移量
        }
        else if(ctx->in_pes && !pusi)
        {
            /* continuation of PES data. copy the current contents to the packet
             * buffer, and resize if necessary. 意味着仍然是当前的PES包数据，紧接着上次的写入缓存区*/
            if((ctx->offset + TS_PACKET_DATA_SIZE) > ctx->size) //如果数据超过当前缓存区长度，空间增大一倍
            {
                ctx->size *= 2;
                if((ctx->buf = realloc(ctx->buf, ctx->size)) == NULL) exit(2);
            }
            memcpy(ctx->buf + ctx->offset, pes_data, TS_PACKET_DATA_SIZE);
            ctx->offset += TS_PACKET_DATA_SIZE;
        }
        else if(ctx->in_pes && pusi)
        {
            /* end of a packet that's currently being gathered. fire pes_ready
             * with the current contents and copy the new packet's beginning
             * into the beginning of the buffer. PES包结束，触发处理函数，将新数据包复制到缓冲区开头*/
            pes_ready(ctx->buf, ctx->offset, pes_ready_user_data);
            memcpy(ctx->buf, pes_data, TS_PACKET_DATA_SIZE);
            ctx->offset = TS_PACKET_DATA_SIZE;
        }
    }
}

#define TS_BUFSIZE_NUM_PACKETS 20

/* try to read the TS and parse the TS headers, looking for packets with a
 * matching PID. assemble full PES packets from the found data, according to
 * PUSI flag value. */
static void read_ts(FILE *tsfile, uint16_t pid)
{
    /* try to read TS_BUFSIZE_NUM_PACKETS at once. */
    uint8_t ts_pkts[TS_PACKET_SIZE * TS_BUFSIZE_NUM_PACKETS];
    size_t pkts_read;
    
    /* context for assembling the PES data. */
    pes_assemble_ctx pes_ctx;   //每个元素有8bit
    pes_ctx.size = 2048;
    pes_ctx.offset = 0;
    pes_ctx.in_pes = 0;
    if((pes_ctx.buf = malloc(pes_ctx.size)) == NULL) exit(2);
    
    unsigned int pkt_num = 0;
    
    while((pkts_read = fread(ts_pkts, TS_PACKET_SIZE,
        TS_BUFSIZE_NUM_PACKETS, tsfile)) > 0)  //将数据读入缓存区
    {
        for(size_t i = 0 ; i < pkts_read ; ++i)
        {
            assemble_pes_from_ts(&pes_ctx, ts_pkts + (i * TS_PACKET_SIZE), pid,
                parse_ttx_pes, &pkt_num);  //参数含义：存储地址；各TS包的起始地址；PID；处理函数；
        }
    }
    free(pes_ctx.buf);
}

#undef TS_BUFSIZE_NUM_PACKETS

/* the largest valid PID value. */
#define PID_MAX ((2 << 13) - 1)

/* parses the arguments and writes the results of the parsing to 'pid' and
 * 'tsfile'. in case of a failure, a message is printed to stderr. if
 * successful, 0 is returned, 'pid' contains a valid PID, and 'tsfile' is a
 * handle to a file opened for reading in binary mode. */
static int parse_args(int argc, char **argv, uint16_t *pid, FILE **tsfile)
{
    if(argc != 3)
    {
        fprintf(stderr, "Usage : %s <pid> <tsfile>\n", argv[0]);
        return 1;
    }
    
    /* parse the PID. */
    char *endptr;
    unsigned long ttx_pid = strtoul(argv[1], &endptr, 10); //将字符转换成base进制的整数
    if(ULONG_MAX == ttx_pid || endptr == argv[1] || *endptr != '\0')
    {
        fprintf(stderr, "Could not convert %s to an unsigned integer.\n",
            argv[1]);
        return 1;
    }
    else if(ttx_pid > PID_MAX)
    {
        fprintf(stderr, "Value %lu is too large for a valid PID.\n", ttx_pid);
        return 1;
    }
    
    /* try opening the file. */
    FILE *f = fopen(argv[2], "rb");
    if(NULL == f)
    {
        fprintf(stderr, "Could not open file %s : %s\n", argv[2],
            strerror(errno));
        return 1;
    }
    
    /* everything's ok, write out the arguments. */
    *pid = (ttx_pid & 0x1fff);
    *tsfile = f;
    return 0;
}

#undef PID_MAX

int main(int argc, char **argv)
{
    uint16_t pid;
    FILE *tsfile;
    int rv = parse_args(argc, argv, &pid, &tsfile);
    if(rv != 0)
    {
        return rv;
    }
    read_ts(tsfile, pid);
    
    fclose(tsfile);
    return 0;
}
