#define COMPASS_ADDR 0xfe

void Compass_Setup(void);

void Compass_Reset(void);

bool Compass_Read(uint8_t reg, uint16_t *out);
bool Compass_Write(uint8_t reg, uint8_t value);

int Compass_GetState(void);

extern bool read_compass_request;
extern uint8_t compass_lock;
