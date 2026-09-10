#include "wwvb_frame.h"

Frame::Frame()
{
    this->reset();
}

int Frame::capturedBitCount()
{
    return this->frame_buffer_index;
}

bool Frame::isValid()
{
    // example valid frame characters
    //     |minutes-|hours----|doy-----------|DUTs|DUT-|year-----|LYI-|
    //     |T   O   |T   O    |H    T    O   |    |    |T    O   |    |
    // XXXXF...S....P....S....P....S....P....S....P....S....P....S....P
    //           111111111122222222223333333333444444444455555555556666
    // 0123456789012345678901234567890123456789012345678901234567890123
    //     |   S    |    S    |    S    P    |    |    |    P    |    |
    // ----F00100010P000000100P001000101P000100101P000000010P011000011P
    // ----F01001001P000100111P001000101P000100101P000000010P011000011P

    bool rval = true;
    // beginning of frame
    if (frame[4] != 'F')
    {
        Serial.print(" fF ");
        rval = false;
    }
    // interfield spaces
    else if (frame[8] != '0' || frame[18] != '0' || frame[28] != '0' || frame[38] != '0' || frame[48] != '0' || frame[58] != '0')
    {
        Serial.print(" fS ");
        rval = false;
    }
    // markers
    else if (frame[13] != 'P' || frame[23] != 'P' || frame[33] != 'P' || frame[43] != 'P' || frame[53] != 'P' || frame[63] != 'P')
    {
        Serial.print(" fP ");
        rval = false;
    }
    // always 0 values
    else if (frame[14] != '0' || frame[15] != '0' || frame[24] != '0' || frame[25] != '0' || frame[39] != '0')
    {
        Serial.print(" f0 ");
        rval = false;
    }

    return rval;
}

char Frame::BitTypeToChar(BitTypes value)
{
    char rval = '?';
    switch (value)
      {
      case BAD:
        // bad antenna alignment probably
        rval = 'A';
        break;
      case ZERO:
        rval = '0';
        break;
      case ONE:
        rval = '1';
        break;
      case MARKER:
        rval = 'P';
        break;
      case FRAME:
        rval = 'F';
        break;
      case UNKNOWN:
        rval = '?';
        break;
      default:
        break;
      }

    return rval;
}

int Frame::add(BitTypes value)
{
    if (frame_buffer_index < FRAME_BUFF_CHARS)
    {
        frame[frame_buffer_index] = BitTypeToChar(value);
        frame_buffer_index += 1;

        frame_val <<= 1;
        frame_val += (value == BitTypes::ONE) ? 1 : 0;
    }
    else
    {
        Serial.printf("\n*** EXCEPTION ***\nFrame index exceeds frame buffer length.  Resetting.\n");
        reset();
    }

    return frame_buffer_index;
}

void Frame::reset()
{
    frame_buffer_index = 4; // start at the fourth character
    frame_val = 0b0000000000000000000000000000000000000000000000000000000000000000;
    memset(frame, '-', sizeof(char) * FRAME_BUFF_CHARS);
}

void Frame::printFrame()
{
    Serial.println("            |minutes-|hours----|doy-----------|DUTs|DUT-|year-----|LYI-|");
    Serial.println("            |T   O   |T   O    |H    T    O   |    |    |T    O   |    |");
    Serial.println("        XXXXF...S....P....S....P....S....P....S....P....S....P....S....P");
    Serial.print("binary: ");

    for (int i = sizeof(uint64_t) * 8 - 1; i >= 0; i--)
    {
        Serial.printf("%1d", ((frame_val >> i) & 1));
    }

    Serial.println();
    Serial.print(" chars: ");

    for (int i = 0; i < FRAME_BUFF_CHARS; Serial.print(frame[i]), i++)
        ;
}

int Frame::minutes()
{
    int rval = 0;
    //     |minutes-|hours----|doy-----------|DUTs|DUT-|year-----|LYI-|
    //     |T   O   |T   O    |H    T    O   |    |    |T    O   |    |
    // XXXXF...S....P....S....P....S....P....S....P....S....P....S....P
    // 0000010101001000000001100010001010000100101000000001000110000110
    uint8_t tens = ((frame_val & 0b0000011100000000000000000000000000000000000000000000000000000000) >> 56) & 0x0F;
    uint8_t ones = ((frame_val & 0b0000000001111000000000000000000000000000000000000000000000000000) >> 51) & 0x0F;

    rval = tens * 10 + ones;

    return rval;
}

int Frame::hours()
{
    int rval = 0;
    //     |minutes-|hours----|doy-----------|DUTs|DUT-|year-----|LYI-|
    //     |T   O   |T   O    |H    T    O   |    |    |T    O   |    |
    // XXXXF...S....P....S....P....S....P....S....P....S....P....S....P
    // 0000010101001000000001100010001010000100101000000001000110000110
    uint8_t tens = ((frame_val & 0b0000000000000011110000000000000000000000000000000000000000000000) >> 46) & 0x0F;
    uint8_t ones = ((frame_val & 0b0000000000000000000111100000000000000000000000000000000000000000) >> 41) & 0x0F;

    rval = tens * 10 + ones;

    return rval;
}

int Frame::doy()
{
    int rval = 0;
    //     |minutes-|hours----|doy-----------|DUTs|DUT-|year-----|LYI-|
    //     |T   O   |T   O    |H    T    O   |    |    |T    O   |    |
    // XXXXF...S....P....S....P....S....P....S....P....S....P....S....P
    // 0000010101001000000001100010001010000100101000000001000110000110
    uint8_t huns = ((frame_val & 0b0000000000000000000000001111000000000000000000000000000000000000) >> 36) & 0x0F;
    uint8_t tens = ((frame_val & 0b0000000000000000000000000000011110000000000000000000000000000000) >> 31) & 0x0F;
    uint8_t ones = ((frame_val & 0b0000000000000000000000000000000000111100000000000000000000000000) >> 26) & 0x0F;

    rval = huns * 100 + tens * 10 + ones;

    return rval;
}

int Frame::dutMs()
{
    int rval = 0;
    int sign = 1; // 1 = positive, -1 = negative
                  //     |minutes-|hours----|doy-----------|DUTs|DUT-|year-----|LYI-|
                  //     |T   O   |T   O    |H    T    O   |    |    |T    O   |    |
                  // XXXXF...S....P....S....P....S....P....S....P....S....P....S....P
                  // 0000010101001000000001100010001010000100101000000001000110000110
    uint8_t duts = ((frame_val & 0b0000000000000000000000000000000000000001111000000000000000000000) >> 21) & 0x0F;
    uint8_t dutv = ((frame_val & 0b0000000000000000000000000000000000000000000011110000000000000000) >> 16) & 0x0F;

    if ((duts & 0b00000111) == 0b00000101)
        sign = 1;
    else if ((duts & 0b00000111) == 0b00000010)
        sign = -1;

    rval = duts * dutv * 100; // DUT value is 1/10th seconds so multiply by 100 to get milliseconds

    return rval;
}

int Frame::year()
{
    int rval = 0;
    //     |minutes-|hours----|doy-----------|DUTs|DUT-|year-----|LYI-|
    //     |T   O   |T   O    |H    T    O   |    |    |T    O   |    |
    // XXXXF...S....P....S....P....S....P....S....P....S....P....S....P
    // 0000010101001000000001100010001010000100101000000001000110000110
    uint8_t tens = ((frame_val & 0b0000000000000000000000000000000000000000000000000111100000000000) >> 11) & 0x0F;
    uint8_t ones = ((frame_val & 0b0000000000000000000000000000000000000000000000000000001111000000) >> 6) & 0x0F;

    rval = tens * 10 + ones;
    return rval;
}

bool Frame::isDST()
{
    //     |minutes-|hours----|doy-----------|DUTs|DUT-|year-----|LYI-|
    //     |T   O   |T   O    |H    T    O   |    |    |T    O   |    |
    // XXXXF...S....P....S....P....S....P....S....P....S....P....S....P
    // 0000010101001000000001100010001010000100101000000001000110000110
    uint8_t ones = ((frame_val & 0b0000000000000000000000000000000000000000000000000000000000000110) >> 1) & 0x0F;
    return (ones == 3);
}

Fields Frame::fieldValues()
{
    _fields_helper.frame_val = frame_val;

    return _fields_helper.frame_fields;
}

void Frame::printFrameValues()
{
    Fields f = this->fieldValues();

    Serial.println();
    Serial.printf("Year: %2d %2d\n", f.y10, f.y1);
    Serial.printf("DOY: %3d %2d %2d\n", f.d100, f.d10, f.d1);
    Serial.printf("Hour: %2d %2d\n", f.h10, f.h1);
    Serial.printf("Minutes: %2d %2d\n", f.m10, f.m1);
    Serial.printf("DUTS: 0x%0x\n", f.duts);
    Serial.printf("DUT: 0x%0x\n", f.dut);
}

bool Frame::frameTime(struct tm *ft, int offset = 0)
{
    struct tm t;

    time_t now = time(NULL);
    gmtime_r(&now, &t);

    t.tm_min = this->minutes(); // The frame describes the previous minute
    t.tm_hour = this->hours();
    t.tm_sec = 1;
    t.tm_mday = 1;
    t.tm_mon = 0;
    t.tm_mday = 0;
    t.tm_yday = 0;
    t.tm_year = 100 + this->year(); // +100 == start at 2000
    t.tm_isdst = this->isDST();

    // t is now Jan 1 of the year @ hours:minutes past midnight.

    time_t ref = mktime(&t);

    // now add the day of the year and offset to the epoch that is t
    time_t day = ref + (this->doy() * 86400) + offset * 60; // tm uses 0-based months

    if (ft != nullptr)
    {
        gmtime_r(&day, ft);
        return true;
    }

    return false;
}