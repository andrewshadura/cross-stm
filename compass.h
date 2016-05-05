#define COMPASS_ADDR 0xfe

void Compass_Setup(void);

void Compass_Reset(void);

bool Compass_Read(uint8_t reg, uint16_t *out);

extern bool read_compass_request;
