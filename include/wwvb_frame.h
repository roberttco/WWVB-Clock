#define FRAME_BUFF_CHARS 64

#include <Arduino.h>

struct Fields
{
    // nomenclature: s_xx = spacer at bit location xx, p_xx = marker at bit location xx
    uint8_t cruft: 4;     // 0000
    uint8_t frm : 1;      // 00

    uint8_t m10 : 3;      // 01-03
    uint8_t s_04 : 1;     // 04
    uint8_t m1 : 4;       // 05-08
    uint8_t p_09 : 1;     // 09

    uint8_t h10 : 4;      // 10-13
    uint8_t s_14 : 1;     // 14
    uint8_t h1 : 4;       // 15-18
    uint8_t p_19 : 1;     // 19

    uint8_t d100 : 4;     // 20-23
    uint8_t s_24 : 1;     // 24
    uint8_t d10 : 4;      // 25-28
    uint8_t p_29 : 1;     // 29

    uint8_t d1 : 4;       // 30-33
    uint8_t s_34 : 1;     // 34
    uint8_t duts : 4;     // 35-38
    uint8_t p_39 : 1;     // 39

    uint8_t dut : 4;      // 40-43
    uint8_t s_44 : 1;     // 44
    uint8_t y10 : 4;      // 45-48
    uint8_t p_49 : 1;     // 49
    uint8_t y1 : 4;       // 50-53
    uint8_t s_54 : 1;     // 54
    uint8_t lyi : 4;      // 55-58
    uint8_t p_59 : 1;     // 59
};

union FieldsHelper
{
    uint64_t frame_val;
    Fields frame_fields;
};

#define FRAME_BUFF_CHARS 64

enum BitTypes
{
  ZERO = 0,
  ONE = 1,
  MARKER = 2,
  FRAME = 3,
  INTERFIELD_SPACE = 4,
  BAD = 253,
  WAITING = 254,
  UNKNOWN = 255
};

class Frame
{
public:
    Frame();

    int add(BitTypes value);
    void reset();
    bool isValid();
    void printframe();
    void printFrameVal();
    char BitTypeToChar(BitTypes value);

    int hours();
    int minutes();
    int doy();
    int year();
    bool isDST();
    int dutMs();

    void printFrame();
    void printFrameValues();
    Fields fieldValues();

    bool frameTime(struct tm *t, int offset);

    int capturedBitCount();

private:
    uint64_t frame_val;
    char frame[FRAME_BUFF_CHARS];
    FieldsHelper _fields_helper;
    uint8_t frame_buffer_index;

};
